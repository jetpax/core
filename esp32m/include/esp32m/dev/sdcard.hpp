#pragma once

#include <hal/gpio_types.h>

#include "esp32m/device.hpp"
#include "esp32m/events/request.hpp"
#include "esp32m/io/pins.hpp"
#include <esp32m/io/gpio.hpp>

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

    class Sdcard : public Device {
     public:
      enum class State { Removed = 0, Inserted = 1 };
      Sdcard(const Sdcard &) = delete;
      
      static Sdcard &instance();
      const char *name() const override {
        return "sdcard";
      }
      static const char *toString(State s);
      State state() {
        return refreshState();
      }
      const char *stateName();

    protected:
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool handleRequest(Request &req) override;

     private:
      Sdcard() {
        init();
      }; 
      void init();
      io::pin::IDigital *_pinCd = gpio::pin(GPIO_NUM_3)->digital();
      unsigned long _stamp = 0;
      State _state = State::Removed;
      void setState(State state);
      State refreshState();
    };

    Sdcard *useSdcard();

  }  // namespace dev
}  // namespace esp32m