#pragma once

#include <hal/gpio_types.h>

#include "esp32m/device.hpp"
#include "esp32m/events/request.hpp"
#include "esp32m/io/pins.hpp"
#include <esp32m/io/gpio.hpp>
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"

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
      esp_err_t mount();
      esp_err_t unmount(bool h);
      sdmmc_host_t _host;
      sdmmc_slot_config_t _slotConfig;
      esp_vfs_fat_sdmmc_mount_config_t _mountConfig;
      sdmmc_card_t* _card;
      bool _mounted;
      bool _unmounting;
      static constexpr const char _mount_point[] = "/sdcard";

    protected:
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool handleRequest(Request &req) override;
      bool pollSensors() override;
      bool initSensors() override;

     private:
      Sdcard() {
        Device::init(Flags::HasSensors);
      }; 
      esp_err_t init();
      io::pin::IDigital *_pinCd = gpio::pin((gpio_num_t)CONFIG_SDCARD_CD)->digital();
      State _state = State::Removed;
      void setState(State state);
    };

    Sdcard *useSdcard();

  }  // namespace dev
}  // namespace esp32m