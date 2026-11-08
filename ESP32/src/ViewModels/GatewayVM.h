#pragma once
#include "../Models/Command.h"
#include "../Models/PlcModel.h"
#include "../Services/ModbusService.h"
#include "../Services/MqttService.h"
#include "../Utils/Signal.h"

class GatewayVM {
 public:
  // Upward Signals to View (Rule 5.1.C)
  Signal<const PlcData&>                     dataChanged;
  Signal<const char*, bool, const char*, uint16_t> ackReady;

  GatewayVM(PlcModel& model,
            CommandQueue& queue,
            ModbusService& modbus,
            MqttService& mqtt);

  void submitCommand(const Command& cmd);
  void changeBaudrate(uint32_t newBaud);
  void onMqttConnected();
  void tick();

 private:
  void executeCommand(const Command& c);

  PlcModel&      model_;
  CommandQueue&  queue_;
  ModbusService& modbus_;
  MqttService&   mqtt_;

  unsigned long lastPollTime_    = 0;
  unsigned long lastPublishTime_ = 0;
  bool          forcePublish_    = true;
};
