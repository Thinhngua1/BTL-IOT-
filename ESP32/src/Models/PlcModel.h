#pragma once
#include <stdint.h>

struct PlcData {
  uint16_t    count     = 0;
  uint16_t    target    = 0;
  bool        running   = false;
  bool        modbusOk  = false;
  const char* lastError = "Not polled yet";
};

class PlcModel {
 public:
  // Updates model state and returns true if any key operational field changed
  bool update(const PlcData& next) {
    bool changed = !initialized_ ||
                   (state_.count != next.count) ||
                   (state_.running != next.running) ||
                   (state_.target != next.target) ||
                   (state_.modbusOk != next.modbusOk);
    state_ = next;
    initialized_ = true;
    return changed;
  }

  const PlcData& data() const { return state_; }

 private:
  PlcData state_{};
  bool    initialized_ = false;
};
