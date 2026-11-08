#include "ModbusService.h"
#include <driver/gpio.h>
#include "../Config.h"
#include "../PlcMap.h"

void ModbusService::begin(HardwareSerial& serial,
                          uint8_t slaveId,
                          uint32_t baud,
                          uint32_t config,
                          uint8_t rxPin,
                          uint8_t txPin) {
  serial_  = &serial;
  slaveId_ = slaveId;
  baud_    = baud;
  config_  = config;
  rxPin_   = rxPin;
  txPin_   = txPin;

  serial_->begin(baud_, config_, rxPin_, txPin_);
  gpio_pullup_en((gpio_num_t)rxPin_);
  node_.begin(slaveId_, *serial_);
}

void ModbusService::setBaudrate(uint32_t newBaud) {
  if (!serial_) return;
  baud_ = newBaud;
  serial_->end();
  delay(20);
  serial_->begin(baud_, config_, rxPin_, txPin_);
  gpio_pullup_en((gpio_num_t)rxPin_);
  node_.begin(slaveId_, *serial_);
}

const char* ModbusService::errorString(uint8_t result) {
  switch (result) {
    case ModbusMaster::ku8MBSuccess:            return "OK (Thanh cong)";
    case ModbusMaster::ku8MBIllegalFunction:    return "Loi 01: Sai Function Code";
    case ModbusMaster::ku8MBIllegalDataAddress: return "Loi 02: Sai dia chi thanh ghi/coil";
    case ModbusMaster::ku8MBIllegalDataValue:   return "Loi 03: Gia tri du lieu khong hop le";
    case ModbusMaster::ku8MBSlaveDeviceFailure: return "Loi 04: Loi thiet bi Slave (PLC)";
    case ModbusMaster::ku8MBInvalidSlaveID:     return "Loi E0: Sai Slave ID";
    case ModbusMaster::ku8MBInvalidFunction:    return "Loi E1: Function khong hop le";
    case ModbusMaster::ku8MBResponseTimedOut:   return "Loi E2: Timeout (PLC chua phan hoi)";
    case ModbusMaster::ku8MBInvalidCRC:         return "Loi E3: Sai ma CRC";
    default:                                    return "Loi khong xac dinh";
  }
}

uint8_t ModbusService::pulseCoil(uint16_t coilAddress) {
  uint8_t res = node_.writeSingleCoil(coilAddress, 1);
  if (res != ModbusMaster::ku8MBSuccess) return res;

  delay(50);

  res = node_.writeSingleCoil(coilAddress, 0);
  return res;
}

uint8_t ModbusService::writeTarget(uint16_t value) {
  return node_.writeSingleRegister(PlcMap::REG_D200_TARGET, value);
}

bool ModbusService::readAll(PlcData& out) {
  uint8_t res;

  // 1. Read D100 (FC 03)
  res = node_.readHoldingRegisters(PlcMap::REG_D100_COUNT, 1);
  if (res != ModbusMaster::ku8MBSuccess) {
    out.modbusOk  = false;
    out.lastError = errorString(res);
    return false;
  }
  out.count = node_.getResponseBuffer(0);

  delay(10);

  // 2. Read M100 (FC 01)
  res = node_.readCoils(PlcMap::COIL_M100_STATUS, 1);
  if (res != ModbusMaster::ku8MBSuccess) {
    out.modbusOk  = false;
    out.lastError = errorString(res);
    return false;
  }
  out.running = (node_.getResponseBuffer(0) & 0x01) != 0;

  delay(10);

  // 3. Read D200 (FC 03)
  res = node_.readHoldingRegisters(PlcMap::REG_D200_TARGET, 1);
  if (res != ModbusMaster::ku8MBSuccess) {
    out.modbusOk  = false;
    out.lastError = errorString(res);
    return false;
  }
  out.target = node_.getResponseBuffer(0);

  out.modbusOk  = true;
  out.lastError = "";
  digitalWrite(Config::ONBOARD_LED, !digitalRead(Config::ONBOARD_LED));
  return true;
}
