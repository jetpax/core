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

      // ensure all fields in _mountConfig are initialized to zero
      memset(&_mountConfig,0,sizeof(esp_vfs_fat_sdmmc_mount_config_t));
      // Options for mounting the filesystem.
      // If format_if_mount_failed is set to true, SD card will be partitioned and
      // formatted in case when mounting fails.
      _mountConfig = {
  #ifdef CONFIG_SDCARD_FORMAT_IF_MOUNT_FAILED
          .format_if_mount_failed = true,
  #else
          .format_if_mount_failed = false,
  #endif // EXAMPLE_FORMAT_IF_MOUNT_FAILED
          .max_files = 5,
          .allocation_unit_size = 16 * 1024
      };
      logI("Initializing SD card using SDMMC peripheral");

      // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
      // For setting a specific frequency, use host.max_freq_khz (range 400kHz - 40MHz for SDMMC)
      // Example: for fixed frequency of 10MHz, use _host.max_freq_khz = 10000;
      _host = SDMMC_HOST_DEFAULT();

      // This initializes the slot without card detect (CD) and write protect (WP) signals.
      // We handle Card Detect directly
      _slotConfig = SDMMC_SLOT_CONFIG_DEFAULT();
          // Set bus width to use:
      #ifdef CONFIG_SDMMC_BUS_WIDTH_4
          _slotConfig.width = 4;
      #else
          _slotConfig.width = 1;
      #endif
          // On chips where the GPIOs used for SD card can be configured, set them in
          // the _slotConfig structure:
      #ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
          _slotConfig.clk = (gpio_num_t) CONFIG_SDMMC_CLK;
          _slotConfig.cmd = (gpio_num_t) CONFIG_SDMMC_CMD;
          _slotConfig.d0 = (gpio_num_t) CONFIG_SDMMC_D0;
      #ifdef CONFIG_SDMMC_BUS_WIDTH_4
          _slotConfig.d1 = (gpio_num_t) CONFIG_SDMMC_D1;
          _slotConfig.d2 = (gpio_num_t) CONFIG_SDMMC_D2;
          _slotConfig.d3 = (gpio_num_t) CONFIG_SDMMC_D3;
      #endif  // CONFIG_SDMMC_BUS_WIDTH_4
      #endif  // CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
      _mounted = false;
      _unmounting = false;
      return ESP_OK;
    }

    const char *Sdcard::toString(State s) {
      static const char *names[] = {"Removed", "Inserted"};
      int si = (int)s;
      if (si < 0 || si > 1)
        si = 0;
      return names[si];
    }

    void Sdcard::setState(State state) {
      if (state == _state)
        return;
      logI("state changed: %s -> %s", toString(_state), toString(state));
      _state = state;
      if (state == Sdcard::State::Inserted) mount();
      else unmount(1);
      StaticJsonDocument<0> doc;
      doc.set(serialized(toString(state)));
      EventStateChanged::publish(this, doc);
    }

    DynamicJsonDocument *Sdcard::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(1));
      JsonObject info = doc->to<JsonObject>();
      info["state"] = toString(_state);
      return doc;
    }

    bool Sdcard::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      return false;
    }

    bool Sdcard::pollSensors() {
      State state;
      bool cdLevel = false;
      _pinCd->read(cdLevel);
      state = cdLevel ? Sdcard::State::Removed : Sdcard::State::Inserted;
      setState(state);      
      return true;
    }

    esp_err_t Sdcard::mount() {
      if (_unmounting)
        {
        logE("mount failed: unmount in progress");
        return ESP_FAIL;
        }
      else if (_mounted)
        {
        logE("mount failed: already mounted");
        return ESP_FAIL;
        }

      // _host.max_freq_khz = MyConfig.GetParamValueInt("sdcard", "maxfreq.khz", 16000);
       esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sd", &_host, &_slotConfig, &_mountConfig, &_card);
      if (ret == ESP_OK)
        {
        logI("sdcard mounted at %s", _mount_point);
        // Card has been initialized, print its properties
        sdmmc_card_print_info(stdout, _card);
        _mounted = true;
        _unmounting = false;
        // mountcount = 3;
        }
      else
        {
        logE("mount failed: %s", esp_err_to_name(ret));
        }
      return ret;
      }

    esp_err_t Sdcard::unmount(bool hard /*=false*/) {
      if (!_mounted)
        return ESP_OK;
      // if (!hard)
      //   {
      //   if (!_unmounting)
      //     {
      //     m_unmounting = true;
      //     logD("unmount: preparing");
      //     OvmsEvents::instance(TAG).SignalEvent("sd.unmounting", NULL, sdcard_unmounting_done);
      //     }
      //   return ESP_FAIL;
      //   }
      // else
      //   {
        esp_err_t ret = esp_vfs_fat_sdmmc_unmount();
        // esp_err_t ret = esp_vfs_fat_sdcard_unmount(_mount_point, _card);
        if (ret == ESP_OK)
          {
          logI("unmount done");
          _mounted = false;
          _unmounting = false;
          // mountcount = 3;
          }
        else
          {
          logE("unmount failed: %s", esp_err_to_name(ret));
          }
        return ret;
        // }
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
