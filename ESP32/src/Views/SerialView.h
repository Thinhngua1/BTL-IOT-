#pragma once
#include <Arduino.h>
#include "../Models/PlcModel.h"
#include "../ViewModels/GatewayVM.h"

class SerialView {
 public:
  explicit SerialView(GatewayVM& vm) : vm_(vm) {}

  void printBanner();
  void pollInput();

  // Slots connected to ViewModel Signals (Rule 5.1.C)
  void onDataChanged(const PlcData& d);
  void onAckReady(const char* cmdName, bool ok, const char* errMsg, uint16_t value);

 private:
  GatewayVM& vm_;
};
