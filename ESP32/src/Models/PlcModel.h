#pragma once
#include <stdint.h>

// PlcData la kieu du lieu tu dinh nghia de gom cac thong tin doc duoc tu PLC.
// Vi du: PlcData data; tao bien data chua mot bo thong tin PLC.
struct PlcData {
  // So san pham hien tai doc tu thanh ghi D100.
  uint16_t    count     = 0;
  // So san pham muc tieu doc tu thanh ghi D200.
  uint16_t    target    = 0;
  // Trang thai bang tai: true la dang chay, false la dang dung.
  bool        running   = false;
  // Trang thai giao tiep Modbus: true neu doc PLC thanh cong.
  bool        modbusOk  = false;
  // Mo ta loi Modbus gan nhat, neu co.
  const char* lastError = "Not polled yet";
};

class PlcModel {
 public:
  // Cap nhat du lieu PLC va tra ve true neu cac thong tin chinh co thay doi.
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
