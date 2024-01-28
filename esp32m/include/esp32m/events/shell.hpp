#pragma once

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

      static void publish(std::string msg) {
        Shell ev(msg);
        ev.Event::publish();
      }

      std::string msg() const {
        return _msg;
      }

     private:
      std::string _msg;
      constexpr static const char *Type = "shell";
      Shell(std::string msg) : Event(Type), _msg(msg){};
    };

  }  // namespace event
}  // namespace esp32m