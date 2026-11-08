#pragma once
#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include "../Models/Command.h"
#include "../Models/PlcModel.h"
#include "../Utils/Signal.h"

class MqttService {
 public:
  // Upward Signals to ViewModel (Rule 5.1.B)
  Signal<const Command&> commandReceived;
  Signal<>               connected;

  void begin();
  void loop();

  bool publishData(const PlcData& data);
  void publishAck(const char* cmdName, bool ok, const char* errMsg, uint16_t value);

 private:
  void onMessage(char* topic, byte* payload, unsigned int length);

  WiFiClient    wifiClient_;
  PubSubClient  mqtt_{wifiClient_};
  String        clientId_;
  unsigned long lastRetryMs_      = 0;
  bool          wifiWasConnected_ = false;
};
