#pragma once

#include <esp32m/events/shell.hpp>

#include <map>
#include <mutex>

namespace esp32m {
  namespace ui {
    class Shell {
     public:
      Shell(const Shell &) = delete;
      static Shell &instance();

     private:
      Shell();
      std::map<uint8_t, uint8_t> _map;
      std::mutex _mutex;
    };
  }  // namespace ui
}  // namespace esp32m
