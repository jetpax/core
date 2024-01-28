#include "esp32m/events/shell.hpp"
#include "esp32m/debug/shell.hpp"
#include "esp32m/events.hpp"

namespace esp32m {
  namespace ui {

    Shell::Shell() {
      EventManager::instance().subscribe([this](Event &ev) {
        event::Shell *shell;
        if (event::Shell::is(ev, &shell)) {  
          logi("wshell event: %s", shell->msg().c_str()) ;
          std::lock_guard guard(_mutex);
        }
      });
    }


    Shell &Shell::instance() {
      static Shell i;
      return i;
    }

  }  // namespace ui
}  // namespace esp32m
