#include <Arduino.h>
#include "Config.h"
#include "Models/Command.h"
#include "Models/PlcModel.h"
#include "Services/ModbusService.h"
#include "Services/MqttService.h"
#include "ViewModels/GatewayVM.h"
#include "Views/SerialView.h"

// ============================================================================
// CENTRALIZED MVVM WIRING (Rule 5.2)
// ============================================================================
static PlcModel      plcModel;
static CommandQueue  cmdQueue;
static ModbusService modbusService;
static MqttService   mqttService;
static GatewayVM     gatewayVM(plcModel, cmdQueue, modbusService, mqttService);
static SerialView    serialView(gatewayVM);

void setup() {
  pinMode(Config::ONBOARD_LED, OUTPUT);
  Serial.begin(115200);
  delay(1000);

  // 1. Initialize Services
  modbusService.begin(Serial2,
                      Config::PLC_SLAVE_ID,
                      Config::MODBUS_BAUD,
                      Config::MODBUS_CONFIG,
                      Config::MODBUS_RX_PIN,
                      Config::MODBUS_TX_PIN);
  mqttService.begin();

  // 2. Wire Service -> ViewModel Signals (Rule 5.1.B)
  mqttService.commandReceived.connect([](const Command& c) {
    gatewayVM.submitCommand(c);
  });
  mqttService.connected.connect([]() {
    gatewayVM.onMqttConnected();
  });

  // 3. Wire ViewModel -> View Signals (Rule 5.1.C)
  gatewayVM.dataChanged.connect([](const PlcData& d) {
    serialView.onDataChanged(d);
  });
  gatewayVM.ackReady.connect([](const char* name, bool ok, const char* err, uint16_t val) {
    serialView.onAckReady(name, ok, err, val);
  });

  serialView.printBanner();
}

void loop() {
  mqttService.loop();
  serialView.pollInput();
  gatewayVM.tick();
}
