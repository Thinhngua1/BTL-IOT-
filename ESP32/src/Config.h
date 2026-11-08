#pragma once
#include <Arduino.h>

namespace Config {
// Hardware pins
constexpr uint8_t ONBOARD_LED   = 2;
constexpr uint8_t MODBUS_RX_PIN = 16;
constexpr uint8_t MODBUS_TX_PIN = 17;

// Modbus RTU parameters (verified with PLC Xinje XD2-16T-E COM2)
constexpr uint8_t  PLC_SLAVE_ID  = 2;
constexpr uint32_t MODBUS_BAUD   = 2400;
constexpr uint32_t MODBUS_CONFIG = SERIAL_8E1;

// Timing parameters
constexpr unsigned long POLL_INTERVAL_MS = 1000;
constexpr unsigned long HEARTBEAT_MS     = 5000;
constexpr unsigned long MQTT_RETRY_MS    = 5000;
constexpr uint8_t       CMD_QUEUE_SIZE   = 8;

// MQTT Topics
#define TOPIC_BASE "xinje/plc1"
constexpr const char* TOPIC_DATA   = TOPIC_BASE "/data";
constexpr const char* TOPIC_CMD    = TOPIC_BASE "/cmd";
constexpr const char* TOPIC_ACK    = TOPIC_BASE "/ack";
constexpr const char* TOPIC_STATUS = TOPIC_BASE "/status";
}  // namespace Config
