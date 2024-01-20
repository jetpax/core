#include <driver/gpio.h>

#include "esp32m/app.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/dev/sdcard.hpp"
#include "esp32m/integrations/ha/ha.hpp"


// Full status to return through API
  // SD CARD is inserted
  // Status: available
  // Name: SD32G
  // Size:  29542 MB
  // Free:  29541 MB

  // Card type: SDHC/SDXC
  // Max speed: 25000 kHz
  // Capacity: 29554 MB
  // CSD: ver=1, sector_size=512, capacity=60526592 read_bl_len=9
  // SCR: sd_spec=2, bus_width=5

namespace esp32m {
  namespace dev {

    esp_err_t Sdcard::init() {
      ESP_ERROR_CHECK_WITHOUT_ABORT(_pinCd->setDirection(true, false));
      ESP_ERROR_CHECK_WITHOUT_ABORT(_pinCd->setPull(true, false));
      refreshState();
      return ESP_OK;
    }

    Sdcard::State Sdcard::refreshState() {
      State state;
      bool cdLevel = false;
      _pinCd->read(cdLevel);
      state = cdLevel ? Sdcard::State::Removed : Sdcard::State::Inserted;
      setState(state);
      return _state;
    }

    const char *Sdcard::toString(State s) {
      static const char *names[] = {"Removed", "Inserted"};
      int si = (int)s;
      if (si < 0 || si > 1)
        si = 0;
      return names[si];
    }

    const char *Sdcard::stateName() {
      return toString(refreshState());
    }

    void Sdcard::setState(State state) {
      if (state == _state)
        return;
      logI("state changed: %s -> %s", toString(_state), toString(state));
      _state = state;
      StaticJsonDocument<0> doc;
      doc.set(serialized(toString(state)));
      EventStateChanged::publish(this, doc);
    }

    DynamicJsonDocument *Sdcard::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(1));
      JsonObject info = doc->to<JsonObject>();
      info["state"] = toString(refreshState());
      return doc;
    }

    bool Sdcard::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      return false;
    }

    bool Sdcard::pollSensors() {
      // logI("%s", stateName() );
      return true;
    }

    bool Sdcard::initSensors() {
      return Sdcard::init() == ESP_OK;
    }

    Sdcard &Sdcard::instance() {
      static Sdcard i;
      return i;
    }

    Sdcard *useSdcard() {
      return &Sdcard::instance();
    }

  }  // namespace dev
}  // namespace esp32m
