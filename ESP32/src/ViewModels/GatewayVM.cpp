#include "GatewayVM.h"
#include "../Config.h"
#include "../PlcMap.h"

// Ham khoi tao GatewayVM.
// Gan cac doi tuong chinh: model, hang doi lenh, Modbus va MQTT.
GatewayVM::GatewayVM(PlcModel& model,
                     CommandQueue& queue,
                     ModbusService& modbus,
                     MqttService& mqtt)
    : model_(model), queue_(queue), modbus_(modbus), mqtt_(mqtt) {}

// Nhan lenh moi tu giao dien hoac MQTT va dua vao hang doi de xu ly sau.
void GatewayVM::submitCommand(const Command& cmd) {
  if (!queue_.enqueue(cmd.type, cmd.value)) {
    Serial.println("[VM] Command queue full, dropped command");
  }
}

// Thay doi toc do truyen thong Modbus theo yeu cau cau hinh.
void GatewayVM::changeBaudrate(uint32_t newBaud) {
  modbus_.setBaudrate(newBaud);
  Serial.printf("[CONFIG] Modbus baudrate changed to %u bps\n", newBaud);
}

// Khi MQTT ket noi lai, bat dau phat du lieu moi de dam bao dong bo.
void GatewayVM::onMqttConnected() {
  forcePublish_ = true;
}

// Thuc thi mot lenh da duoc lay ra tu hang doi.
// Lenh co the la start, stop, reset hoac dat muc tieu.
void GatewayVM::executeCommand(const Command& c) {
  // Ten lenh va ket qua thuc thi tu Modbus.
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

  // Kiem tra ket qua thuc thi va tao thong diep loi neu can.
  bool ok = (res == ModbusMaster::ku8MBSuccess);
  const char* errStr = ok ? "" : ModbusService::errorString(res);

  // Gui phan hoi ack cho MQTT va cap nhat lai view thong qua signal.
  mqtt_.publishAck(name, ok, errStr, c.value);
  ackReady.emit(name, ok, errStr, c.value);

  // Sau khi thuc thi lenh, lap tuc bat dau doc PLC va phat du lieu moi.
  lastPollTime_ = millis() - Config::POLL_INTERVAL_MS;
  forcePublish_ = true;
}

// Vong lap chinh cua GatewayVM.
// Xu ly lenh, doc du lieu PLC theo chu ki va gui du lieu len MQTT.
void GatewayVM::tick() {
  // Buoc 1: Xu ly hang doi lenh (Downstream: View/MQTT -> PLC)
  Command c;
  while (queue_.dequeue(c)) {
    executeCommand(c);
    mqtt_.loop();
  }

  // Buoc 2: Doc PLC theo chu ki (Upstream: PLC -> Model)
  unsigned long now = millis();
  bool changed = false;
  if (now - lastPollTime_ >= Config::POLL_INTERVAL_MS) {
    lastPollTime_ = now;
    PlcData snapshot = model_.data();
    modbus_.readAll(snapshot);
    changed = model_.update(snapshot);
    dataChanged.emit(model_.data());
  }

  // Buoc 3: Gui du lieu len MQTT khi co thay doi hoac theo heartbeat.
  if (forcePublish_ || changed || (millis() - lastPublishTime_ >= Config::HEARTBEAT_MS)) {
    if (mqtt_.publishData(model_.data())) {
      lastPublishTime_ = millis();
      forcePublish_    = false;
    }
  }
}