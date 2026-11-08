#include "SerialView.h"
#include "../Config.h"

void SerialView::printBanner() {
  Serial.println("\n=======================================================");
  Serial.println("   ESP32 MODBUS RTU <-> MQTT GATEWAY (MVVM ARCHITECTURE)");
  Serial.println("=======================================================");
  Serial.printf ("Publish data   : %s\n", Config::TOPIC_DATA);
  Serial.printf ("Subscribe cmd  : %s\n", Config::TOPIC_CMD);
  Serial.printf ("Modbus Config  : SlaveID=%u, Baud=%u, 8E1\n",
                 Config::PLC_SLAVE_ID, Config::MODBUS_BAUD);
  Serial.println("Serial Cmds    : 's' START | 'p' STOP | 'r' RESET | 't500' TARGET | 'b9600' BAUD");
  Serial.println("=======================================================\n");
}

void SerialView::onDataChanged(const PlcData& d) {
  if (d.modbusOk) {
    Serial.printf("[MONITOR] D100 (Count): %u | M100 (Status): %s | Target (D200): %u\n",
                  d.count,
                  d.running ? "RUNNING (1)" : "STOPPED (0)",
                  d.target);
  } else {
    Serial.printf("[POLL] Modbus error: %s\n", d.lastError ? d.lastError : "Unknown");
  }
}

void SerialView::onAckReady(const char* cmdName, bool ok, const char* errMsg, uint16_t value) {
  if (ok) {
    Serial.printf("[CMD] Executed '%s' (val=%u) -> OK\n", cmdName, value);
  } else {
    Serial.printf("[CMD] Executed '%s' (val=%u) -> FAILED: %s\n",
                  cmdName, value, errMsg ? errMsg : "");
  }
}

void SerialView::pollInput() {
  if (!Serial.available()) return;

  String input = Serial.readStringUntil('\n');
  input.trim();
  if (input.length() == 0) return;

  char cmd = tolower(input.charAt(0));
  switch (cmd) {
    case 's':
      vm_.submitCommand({CmdType::START, 0});
      break;
    case 'p':
      vm_.submitCommand({CmdType::STOP, 0});
      break;
    case 'r':
      vm_.submitCommand({CmdType::RESET, 0});
      break;
    case 't': {
      String valStr = input.substring(1);
      valStr.trim();
      int val = valStr.toInt();
      if (val > 0 && val <= 65535) {
        vm_.submitCommand({CmdType::TARGET, (uint16_t)val});
      } else {
        Serial.println("[WARN] Target must be 1..65535 (e.g. 't500')");
      }
      break;
    }
    case 'b': {
      String bStr = input.substring(1);
      bStr.trim();
      uint32_t newBaud = (uint32_t)bStr.toInt();
      if (newBaud >= 300 && newBaud <= 115200) {
        vm_.changeBaudrate(newBaud);
      } else {
        Serial.println("[WARN] Invalid baudrate (e.g. 'b2400', 'b9600', 'b19200')");
      }
      break;
    }
    default:
      Serial.println("[HELP] Cmds: 's' (Start) | 'p' (Stop) | 'r' (Reset) | 't500' (Target) | 'b9600' (Baud)");
      break;
  }
}
