#include "esp32m/dev/sdcard.hpp"
#include "esp32m/base.hpp"
#include "esp32m/io/gpio.hpp"

#include <esp_task_wdt.h>

namespace esp32m {
  namespace dev {

    Sdcard::Sdcard(gpio_num_t pin) : _pin(gpio::pin(pin)) {
      xTaskCreate([](void *self) { ((Sdcard *)self)->run(); }, "m/dbgbtn", 4096,
                  this, tskIDLE_PRIORITY + 1, &_task);
    }

    void Sdcard::run() {
      _queue = xQueueCreate(16, sizeof(int32_t));
      _pin->digital()->attach(_queue);
      esp_task_wdt_add(NULL);
      int32_t value;
      for (;;) {
        esp_task_wdt_reset();
        if (xQueueReceive(_queue, &value, pdMS_TO_TICKS(1000))) {
          _stamp = millis();
          value = value / 1000;     // get time since last sdcard event in ms
          if (value < 0) {          
            // sdcard removed
            value = -value;
            logI("sdcard inserted for %.2fs", (float)value/1000);
            _cmd = 1;
          } else {                  
            // sdcard inserted
            logI("sdcard removed for %.2fs", (float)value/1000);
             _cmd = 2;
          }
        }
        if (_cmd ) {
          logI("command %x issued", _cmd);
          sdcard::Command ev(_cmd);
          _cmd = 0;
          ev.publish();
        }
      }
    }


    Sdcard *useSdcard(gpio_num_t pin) {
      return new Sdcard(pin);
    }

  }  // namespace dev
}  // namespace esp32m
