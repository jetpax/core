#pragma once

#include "esp32m/app.hpp"
#include "esp32m/events.hpp"
#include "esp32m/io/pins.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace esp32m {
  namespace dev {

    class Sdcard;

    namespace sdcard {
      class Command : public Event {
       public:
        int command() const {
          return _command;
        }
        static bool is(Event &ev, int command) {
          return ev.is(Type) && ((Command &)ev)._command == command;
        }

       private:
        Command(int command) : Event(Type), _command(command) {}
        int _command;
        constexpr static const char *Type = "sdcard-det";
        friend class dev::Sdcard;
      };
    }  // namespace sdcard

    class Sdcard : public AppObject {
     public:
      Sdcard(gpio_num_t pin);
      Sdcard(const Sdcard &) = delete;
      const char *name() const override {
        return "sdcard";
      }

     private:
      io::IPin *_pin;
      TaskHandle_t _task = nullptr;
      QueueHandle_t _queue = nullptr;
      unsigned long _stamp = 0;
      int _cmd = 0;
      void run();
    };

    Sdcard *useSdcard(gpio_num_t pin = GPIO_NUM_0);
  }  // namespace dev
}  // namespace esp32m