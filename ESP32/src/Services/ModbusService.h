#pragma once
#include <Arduino.h>
#include <ModbusMaster.h>
#include "../Models/PlcModel.h"

class ModbusService {
 public:
  void begin(HardwareSerial& serial,
             uint8_t slaveId,
             uint32_t baud,
             uint32_t config,
             uint8_t rxPin,
             uint8_t txPin);

  void setBaudrate(uint32_t newBaud);
  uint32_t baudrate() const { return baud_; }

  bool readAll(PlcData& out);
  uint8_t pulseCoil(uint16_t coilAddress);
  uint8_t writeTarget(uint16_t value);

  static const char* errorString(uint8_t code);

 private:
  HardwareSerial* serial_  = nullptr;
  ModbusMaster    node_;
  uint8_t         slaveId_ = 2;
  uint32_t        baud_    = 2400;
  uint32_t        config_  = SERIAL_8E1;
  uint8_t         rxPin_   = 16;
  uint8_t         txPin_   = 17;
};
