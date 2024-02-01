#pragma once

#include "esp_http_server.h"
#include "esp32m/events.hpp"

namespace esp32m {
  namespace event {

    class Shell : public Event {
     public:
      Shell(const Shell &) = delete;

      static bool is(Event &ev, Shell **r) {
        if (!ev.is(Type))
          return false;
        if (r)
          *r = (Shell *)&ev;
        return true;
      }

      static void publish(httpd_req_t *req, std::string msg) {
        Shell ev(req, msg);
        ev.Event::publish();
      }

      httpd_req_t* req() const {
        return _req;
      }

      std::string msg() const {
        return _msg;
      }

     private:
      httpd_req_t* _req;
      std::string _msg;
      constexpr static const char *Type = "shell";
      Shell(httpd_req_t *req, std::string msg) : Event(Type), _req(req),_msg(msg){};
    };

  }  // namespace event
}  // namespace esp32m