#include "esp32m/ui/httpd.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/logging.hpp"
#include "esp32m/net/mdns.hpp"
#include "esp32m/net/wifi.hpp"
#include "esp32m/ui.hpp"
#include "esp32m/ui/asset.hpp"
#include "esp32m/version.h"

#include <mdns.h>
#include <algorithm>
#include <vector>

namespace esp32m {
  namespace ui {
    const char *UriWs = "/ws";
    const char *UriRoot = "/";
    const char *UriApp = "/app";

    std::vector<Httpd *> _httpdServers;

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define HTTPD_SCRATCH_BUF MAX(HTTPD_MAX_REQ_HDR_LEN, HTTPD_MAX_URI_LEN)
    struct httpd_req_aux {
      struct sock_db *sd; /*!< Pointer to socket database */
      char scratch[HTTPD_SCRATCH_BUF +
                   1]; /*!< Temporary buffer for our operations (1 byte extra
                          for null termination) */
      size_t remaining_len;     /*!< Amount of data remaining to be fetched */
      char *status;             /*!< HTTP response's status code */
      char *content_type;       /*!< HTTP response's content type */
      bool first_chunk_sent;    /*!< Used to indicate if first chunk sent */
      unsigned req_hdrs_count;  /*!< Count of total headers in request packet */
      unsigned resp_hdrs_count; /*!< Count of additional headers in response
                                   packet */
      struct resp_hdr {
        const char *field;
        const char *value;
      } *resp_hdrs; /*!< Additional headers in response packet */
      struct http_parser_url url_parse_res; /*!< URL parsing result, used for
                                               retrieving URL elements */
#ifdef CONFIG_HTTPD_WS_SUPPORT
      bool ws_handshake_detect; /*!< WebSocket handshake detection flag */
      httpd_ws_type_t ws_type;  /*!< WebSocket frame type */
      bool ws_final;            /*!< WebSocket FIN bit (final frame or not) */
      uint8_t mask_key[4];      /*!< WebSocket mask key for this payload */
#endif
    };

    esp_err_t dumpHeaders(httpd_req_t *r) {
      if (r == NULL)
        return ESP_ERR_INVALID_ARG;

      auto ra = (struct httpd_req_aux *)r->aux;
      const char *hdr_ptr = ra->scratch;
      unsigned count = ra->req_hdrs_count;

      while (count--) {
        const char *val_ptr = strchr(hdr_ptr, ':');
        if (!val_ptr)
          break;
        logi("header: %s", hdr_ptr);

        if (count) {
          hdr_ptr = 1 + strchr(hdr_ptr, '\0');
          while (*hdr_ptr == '\0') hdr_ptr++;
        }
      }
      return ESP_OK;
    }

    void freeNop(void *ctx){};

    // Add linger to mitigate "“Unable to create TCP socket: errno 23”
    // see https://docs.espressif.com/projects/espressif-esp-faq/en/latest/software-framework/protocols/lwip.html#after-creating-and-closing-tcp-socket-several-times-an-error-is-reported-as-unable-to-create-tcp-socket-errno-23-how-to-resolve-such-issue
    
    void closeFn(httpd_handle_t hd, int sockfd)
    {
      struct linger so_linger;
      so_linger.l_onoff = true;
      so_linger.l_linger = 0;
      setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
      close(sockfd);
    }

    // For each incoming uri request, httpd calls uriMatcher repeatedly for each uri 
    // that has been registered by httpd_register_uri_handler(): 
    //
    // uriMatcher checks for 
    //  - uri == "/ws", which is matched to the esp32m websocket API interface UriWs, 
    //  - uri starts with "/app", which allows an application to handle additional uris after esp32m has started).
    // 
    // All other uri are matched to the esp32m root handler, UriRoot, for handling by Httpd::incomingReq()
    //
    // Returns:
    //    true if the incoming uri matches the given registered uri 
    //      (httpd then calls the handler for that uri).
    //    
    //    false if no match found 
    //      (httpd then calls this function again with the next registered uri, until all checked)
    //

    bool uriMatcher(const char *registered_uri, const char *incoming_uri, size_t match_upto) {
      logi("Check registered uri: %s \n", registered_uri);

      // check for /ws
      if (strcmp(registered_uri, UriWs)==0){
        if (!strcmp(incoming_uri, UriWs)){
          logi("Matched /ws");
          return true;
        }
        return false;
      }

       // default to root handler
      if (strcmp(registered_uri, UriRoot)==0){
        // if not app
        if (strncmp(incoming_uri, UriApp, strlen(UriApp))){
          logi("Matched system uri %s", incoming_uri);
        return true;
        }
      }

      // check for uris registered by app (uri begins with UriApp)
      if (strncmp(registered_uri, UriApp, strlen(UriApp))==0){
        // perform exact match up to limit
        if (strcmp( incoming_uri, registered_uri) == 0) {
          logi("Matched app uri: %s", incoming_uri);
          return true;
        }
      }

      // try next uri
      return false;   
    }

    esp_err_t wsHandler(httpd_req_t *req) {
      Httpd *httpd = nullptr;
      for (Httpd *i : _httpdServers)
        if (i->_server == req->handle) {
          httpd = i;
          break;
        }
      if (!httpd) {
        logw("no user_ctx: %s %d", req->uri, req->handle);
        return ESP_FAIL;
      }
      return httpd->incomingWs(req);
    }

    esp_err_t httpHandler(httpd_req_t *req) {
      Httpd *httpd = (Httpd *)req->user_ctx;
      if (!httpd) {
        logw("no user_ctx: %s %d", req->uri, req->handle);
        return ESP_FAIL;
      }
      return httpd->incomingReq(req);
    }

    esp_err_t getHeader(httpd_req_t *req, const char *name,
                        std::string &value) {
      auto len = httpd_req_get_hdr_value_len(req, name);
      if (len) {
        size_t bufsize = len + 1;
        auto buf = (char *)malloc(bufsize);
        auto res = httpd_req_get_hdr_value_str(req, name, buf, bufsize);
        if (res == ESP_OK)
          value = buf;
        free(buf);
        return res;
      }
      return ESP_ERR_NOT_FOUND;
    }

    Httpd::Httpd() {
      _config = HTTPD_DEFAULT_CONFIG();
      _config.global_user_ctx = this;
      _config.global_user_ctx_free_fn = freeNop;
      _config.uri_match_fn = uriMatcher;
      _config.close_fn = closeFn;
      _config.lru_purge_enable = true;
      _config.max_uri_handlers = 20;
      // esp_log_level_set("httpd_ws", ESP_LOG_DEBUG);
      _httpdServers.push_back(this);

      net::mdns::Service *mdns = new net::mdns::Service("_http", "_tcp", 80);
      mdns->set("esp32m", ESP32M_VERSION);
      mdns->set("wsapi", "/ws");
      net::Mdns::instance().set(mdns);
    }

    Httpd::~Httpd() {
      _httpdServers.erase(
          std::find(_httpdServers.begin(), _httpdServers.end(), this));
    }

    void Httpd::init(Ui *ui) {
      Transport::init(ui);
      if (ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_start(&_server, &_config)) ==
          ESP_OK) {
        httpd_uri_t uh = {.uri = UriWs,
                          .method = HTTP_GET,
                          .handler = wsHandler,
                          .user_ctx = this,
                          .is_websocket = true,
                          .handle_ws_control_frames = false,
                          .supported_subprotocol = 0};
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(_server, &uh));
        uh.uri = UriRoot;
        uh.is_websocket = false;
        uh.handler = httpHandler;
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(_server, &uh));
      }
    }

    esp_err_t Httpd::incomingReq(httpd_req_t *req) {
      // logI("uri: %s", req->uri);
      // dumpHeaders(req);

      auto cp = false;
      if (!strcmp(req->uri, "/generate_204") ||
          !strcmp(req->uri, "/hotspot-detect.html")) {
        cp = true;
      } else {
        std::string hv;
        if (getHeader(req, "Host", hv) == ESP_OK) {
          // logI("host %s", hv.c_str());
          if (hv == "captive.apple.com" || hv == "detectportal.firefox.com" ||
              hv == "www.msftconnecttest.com")
            cp = true;
        }
      }
      if (cp) {
        net::wifi::Ap *ap = net::Wifi::instance().ap();
        if (ap) {
          char location[32];
          esp_netif_ip_info_t info;
          ap->getIpInfo(info);
          sprintf(location, "http://" IPSTR "/cp", IP2STR(&info.ip));
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              httpd_resp_set_status(req, "302"));  // 307 ???
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              httpd_resp_set_hdr(req, "Location", location));
          // httpd_resp_set_type(req, "text/plain");
          ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_resp_send(req, nullptr, 0));
          logI("redirecting to captive portal %s", location);
          return ESP_OK;
        } else
          logW("got %s request while AP is not running", req->uri);
      }

      Asset *def = nullptr, *found = nullptr;
      for (Asset &a : _ui->assets()) {
        if (!strcmp(req->uri, a._uri)) {
          found = &a;
          break;
        }
        if (!strcmp(a._uri, "/"))
          def = &a;
      }
      if (!found) {
        if (!strEndsWith(req->uri, ".ico"))
          found = def;
        /*logI("strEndsWith %s %s %d %d", req->uri, ".js.map",
             strEndsWith(req->uri, ".js.map"), found == nullptr);*/
      }
      if (!found) {
        logI("404: %s", req->uri);
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_resp_send_404(req));
        return ESP_FAIL;
      }
      char buf[40];
      // logI("serving %s", req->uri);
      if (found->_etag &&
          httpd_req_get_hdr_value_str(req, "If-None-Match", buf, sizeof(buf)) ==
              ESP_OK &&
          !strcmp(buf, found->_etag)) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_resp_set_status(req, "304"));
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_resp_send(req, nullptr, 0));
      } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            httpd_resp_set_type(req, found->_contentType));
        if (found->_contentEncoding)
          ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_resp_set_hdr(
              req, "Content-Encoding", found->_contentEncoding));
        if (found->_etag) {
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              httpd_resp_set_hdr(req, "ETag", found->_etag));
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              httpd_resp_set_hdr(req, "Cache-Control", "no-cache"));
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_resp_send(
            req, (const char *)found->_start, found->_end - found->_start));
      }

      return ESP_OK;
    }

    esp_err_t Httpd::incomingWs(httpd_req_t *req) {
      if (req->method == HTTP_GET) {
        return ESP_OK;
      }
      httpd_ws_frame_t ws_pkt;
      memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
      ESP_CHECK_RETURN(httpd_ws_recv_frame(req, &ws_pkt, 0));

      ws_pkt.payload = (uint8_t *)calloc(1, ws_pkt.len + 1);
      if (!ws_pkt.payload)
        return ESP_ERR_NO_MEM;
      esp_err_t ret = ESP_ERROR_CHECK_WITHOUT_ABORT(
          httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len));
      if (ret == ESP_OK)
        switch (ws_pkt.type) {
          case HTTPD_WS_TYPE_TEXT:
            incoming(httpd_req_to_sockfd(req), ws_pkt.payload, ws_pkt.len);
            break;
          default:
            logI("WS packet type %d was not handled", ws_pkt.type);
            break;
        }
      free(ws_pkt.payload);
      return ret;
    }

    esp_err_t Httpd::wsSend(uint32_t cid, const char *text) {
      httpd_ws_frame_t ws_pkt;
      memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
      ws_pkt.payload = (uint8_t *)text;
      ws_pkt.len = strlen(text);
      ws_pkt.type = HTTPD_WS_TYPE_TEXT;
      return httpd_ws_send_frame_async(_server, cid, &ws_pkt);
    }

  }  // namespace ui
}  // namespace esp32m
