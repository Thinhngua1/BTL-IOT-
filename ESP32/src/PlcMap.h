#pragma once
#include <stdint.h>

// Xinje XD Series 0-based Modbus addresses
namespace PlcMap {
// Upstream (PLC -> ESP32)
constexpr uint16_t REG_D100_COUNT   = 100;  // FC 03: D100 (Current product count)
constexpr uint16_t COIL_M100_STATUS = 100;  // FC 01: M100 (Conveyor running status)

// Downstream (ESP32 -> PLC)
constexpr uint16_t COIL_M0_START   = 0;    // FC 05: M0  (Start command pulse)
constexpr uint16_t COIL_M1_STOP    = 1;    // FC 05: M1  (Stop command pulse)
constexpr uint16_t COIL_M10_RESET  = 10;   // FC 05: M10 (Reset counter pulse)
constexpr uint16_t REG_D200_TARGET = 200;  // FC 06: D200 (Target product count)
}  // namespace PlcMap
