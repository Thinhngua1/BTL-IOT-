#include "GatewayVM.h"
#include "../Config.h"
#include "../PlcMap.h"

GatewayVM::GatewayVM(PlcModel& model,
                     CommandQueue& queue,
                     ModbusService& modbus,
                     MqttService& mqtt)
    : model_(model), queue_(queue), modbus_(modbus), mqtt_(mqtt) {}

void GatewayVM::submitCommand(const Command& cmd) {
  if (!queue_.enqueue(cmd.type, cmd.value)) {
    Serial.println("[VM] Command queue full, dropped command");
  }
}

void GatewayVM::changeBaudrate(uint32_t newBaud) {
  modbus_.setBaudrate(newBaud);
  Serial.printf("[CONFIG] Modbus baudrate changed to %u bps\n", newBaud);
}

void GatewayVM::onMqttConnected() {
  forcePublish_ = true;
}

void GatewayVM::executeCommand(const Command& c) {
  const char* name = "";
  uint8_t res = ModbusMaster::ku8MBSuccess;

  switch (c.type) {
    case CmdType::START:
      name = "start";
      res  = modbus_.pulseCoil(PlcMap::COIL_M0_START);
      break;
    case CmdType::STOP:
      name = "stop";
      res  = modbus_.pulseCoil(PlcMap::COIL_M1_STOP);
      break;
    case CmdType::RESET:
      name = "reset";
      res  = modbus_.pulseCoil(PlcMap::COIL_M10_RESET);
      break;
    case CmdType::TARGET:
      name = "target";
      res  = modbus_.writeTarget(c.value);
      break;
  }

  bool ok = (res == ModbusMaster::ku8MBSuccess);
  const char* errStr = ok ? "" : ModbusService::errorString(res);

  mqtt_.publishAck(name, ok, errStr, c.value);
  ackReady.emit(name, ok, errStr, c.value);

  // Trigger immediate PLC poll after executing command
  lastPollTime_ = millis() - Config::POLL_INTERVAL_MS;
  forcePublish_ = true;
}

void GatewayVM::tick() {
  // 1. Process queued commands (Downstream: View/MQTT -> PLC)
  Command c;
  while (queue_.dequeue(c)) {
    executeCommand(c);
    mqtt_.loop();
  }

  // 2. Poll PLC periodically (Upstream: PLC -> Model)
  unsigned long now = millis();
  bool changed = false;
  if (now - lastPollTime_ >= Config::POLL_INTERVAL_MS) {
    lastPollTime_ = now;
    PlcData snapshot = model_.data();
    modbus_.readAll(snapshot);
    changed = model_.update(snapshot);
    dataChanged.emit(model_.data());
  }

  // 3. Publish to MQTT on change or heartbeat
  if (forcePublish_ || changed || (millis() - lastPublishTime_ >= Config::HEARTBEAT_MS)) {
    if (mqtt_.publishData(model_.data())) {
      lastPublishTime_ = millis();
      forcePublish_    = false;
    }
  }
}
