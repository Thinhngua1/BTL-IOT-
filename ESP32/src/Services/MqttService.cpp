#include "MqttService.h"
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include "../Config.h"
#include "../Secrets.h"

void MqttService::begin() {
  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  delay(100);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_country_code("VN", true);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_15dBm);
  WiFi.setAutoReconnect(true);
  WiFi.begin(Secrets::WIFI_SSID, Secrets::WIFI_PASS);
  Serial.printf("[WIFI] Connecting to '%s' ...\n", Secrets::WIFI_SSID);

  clientId_ = "esp32-xinje-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  mqtt_.setServer(Secrets::MQTT_HOST, Secrets::MQTT_PORT);
  mqtt_.setCallback([this](char* topic, byte* payload, unsigned int len) {
    this->onMessage(topic, payload, len);
  });
  mqtt_.setBufferSize(512);
  mqtt_.setKeepAlive(30);
}

void MqttService::loop() {
  bool wifiNow = (WiFi.status() == WL_CONNECTED);
  if (wifiNow != wifiWasConnected_) {
    wifiWasConnected_ = wifiNow;
    if (wifiNow) {
      Serial.printf("[WIFI] Connected, IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
      Serial.println("[WIFI] Disconnected, reconnecting...");
    }
  }

  if (!wifiNow) {
    unsigned long now = millis();
    if (now - lastRetryMs_ >= Config::MQTT_RETRY_MS) {
      lastRetryMs_ = now;
      Serial.printf("[WIFI] Waiting for '%s' (status=%d)...\n",
                    Secrets::WIFI_SSID, (int)WiFi.status());
    }
    return;
  }
  if (mqtt_.connected()) {
    mqtt_.loop();
    return;
  }

  unsigned long now = millis();
  if (now - lastRetryMs_ < Config::MQTT_RETRY_MS) return;
  lastRetryMs_ = now;

  Serial.printf("[MQTT] Connecting to %s:%u ...\n", Secrets::MQTT_HOST, Secrets::MQTT_PORT);
  bool ok = mqtt_.connect(clientId_.c_str(),
                          Secrets::MQTT_USER[0] ? Secrets::MQTT_USER : nullptr,
                          Secrets::MQTT_PASS[0] ? Secrets::MQTT_PASS : nullptr,
                          Config::TOPIC_STATUS, 1, true, "offline");
  if (ok) {
    Serial.println("[MQTT] Connected!");
    mqtt_.publish(Config::TOPIC_STATUS, "online", true);
    mqtt_.subscribe(Config::TOPIC_CMD, 1);
    connected.emit();
  } else {
    Serial.printf("[MQTT] Failed, state=%d. Retry in %lus\n",
                  mqtt_.state(), Config::MQTT_RETRY_MS / 1000);
  }
}

bool MqttService::publishData(const PlcData& data) {
  if (!mqtt_.connected()) return false;

  JsonDocument doc;
  doc["count"]     = data.count;
  doc["running"]   = data.running;
  doc["target"]    = data.target;
  doc["modbus_ok"] = data.modbusOk;
  if (!data.modbusOk && data.lastError) {
    doc["error"] = data.lastError;
  }

  char buf[192];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  return mqtt_.publish(Config::TOPIC_DATA, (const uint8_t*)buf, n, true);
}

void MqttService::publishAck(const char* cmdName, bool ok, const char* errMsg, uint16_t value) {
  if (!mqtt_.connected()) return;

  JsonDocument doc;
  doc["cmd"] = cmdName;
  if (strcmp(cmdName, "target") == 0) doc["value"] = value;
  doc["ok"] = ok;
  if (!ok && errMsg) doc["error"] = errMsg;

  char buf[192];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  mqtt_.publish(Config::TOPIC_ACK, (const uint8_t*)buf, n, false);
}

void MqttService::onMessage(char* topic, byte* payload, unsigned int length) {
  String cmd;
  long value = 0;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (!err && doc.is<JsonObject>()) {
    // Support both {"cmd":"start"} (Cuong) and {"command":"START"} / {"command":"SET_TARGET"} (Trung HMI)
    if (doc["cmd"].is<const char*>()) {
      cmd = doc["cmd"].as<const char*>();
    } else if (doc["command"].is<const char*>()) {
      cmd = doc["command"].as<const char*>();
    }

    if (!doc["value"].isNull()) {
      value = doc["value"].as<long>();
    } else if (!doc["set_target"].isNull()) {
      value = doc["set_target"].as<long>();
    } else if (!doc["target"].isNull()) {
      value = doc["target"].as<long>();
    }

    if (cmd.isEmpty() && (!doc["set_target"].isNull() || !doc["target"].isNull())) {
      cmd = "target";
    }
  } else {
    cmd.reserve(length);
    for (unsigned int i = 0; i < length; i++) cmd += (char)payload[i];
  }
  cmd.trim();
  cmd.toLowerCase();

  Serial.printf("[MQTT] Rx [%s]: cmd=%s value=%ld\n", topic, cmd.c_str(), value);

  if (cmd == "start") {
    commandReceived.emit(Command{CmdType::START, 0});
  } else if (cmd == "stop") {
    commandReceived.emit(Command{CmdType::STOP, 0});
  } else if (cmd == "reset") {
    commandReceived.emit(Command{CmdType::RESET, 0});
  } else if (cmd == "target" || cmd == "set_target" || cmd == "update") {
    if (value > 0 && value <= 65535) {
      commandReceived.emit(Command{CmdType::TARGET, (uint16_t)value});
    } else {
      Serial.println("[MQTT] Invalid target value (1..65535)");
    }
  } else {
    Serial.println("[MQTT] Unknown command, ignored");
  }
}
