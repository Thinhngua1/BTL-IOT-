#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ModbusMaster.h>

// ============================================================================
// 0. CẤU HÌNH WIFI & MQTT  (SỬA CHO PHÙ HỢP)
// ============================================================================
const char* WIFI_SSID = "Galaxy M2147B9";
const char* WIFI_PASS = "12345678";

const char*        MQTT_HOST = "10.167.232.152";    // IP máy tính chạy Mosquitto (lấy bằng ipconfig)
constexpr uint16_t MQTT_PORT = 1883;
const char*        MQTT_USER = "";                // để "" nếu broker không yêu cầu
const char*        MQTT_PASS = "";

// Cây topic
#define TOPIC_BASE "xinje/plc1"
const char* TOPIC_DATA   = TOPIC_BASE "/data";    // ESP32 -> Server : D100, M100 (JSON)
const char* TOPIC_CMD    = TOPIC_BASE "/cmd";     // Server -> ESP32 : lệnh M0, M1, M10, D200
const char* TOPIC_ACK    = TOPIC_BASE "/ack";     // ESP32 -> Server : kết quả thực thi lệnh
const char* TOPIC_STATUS = TOPIC_BASE "/status";  // online / offline (LWT)

constexpr unsigned long MQTT_RETRY_MS     = 5000;  // thử kết nối lại MQTT mỗi 5s
constexpr unsigned long HEARTBEAT_MS      = 5000;  // gửi dữ liệu định kỳ dù không đổi
constexpr uint8_t       CMD_QUEUE_SIZE    = 8;

// ============================================================================
// 1. CẤU HÌNH PHẦN CỨNG & THÔNG SỐ MODBUS RTU (PLC XINJE XD2-16T-E)
// ============================================================================
constexpr uint8_t ONBOARD_LED   = 2;
constexpr uint8_t MODBUS_RX_PIN = 16;   // Nối vào chân RO của mạch MAX485
constexpr uint8_t MODBUS_TX_PIN = 17;   // Nối vào chân DI của mạch MAX485

constexpr uint8_t  PLC_SLAVE_ID  = 1;
constexpr uint32_t MODBUS_BAUD   = 9600;       // Đổi thành 19200 nếu PLC để mặc định hãng
constexpr uint32_t MODBUS_CONFIG = SERIAL_8N1; // Đổi thành SERIAL_8E1 (Even Parity) nếu PLC để mặc định

// ============================================================================
// 2. BẢNG ĐỊA CHỈ MODBUS (XINJE XD SERIES - 0-BASED ADDRESS)
// ============================================================================
// Chiều PLC -> ESP32 (Đọc)
constexpr uint16_t REG_D100_COUNT   = 100; // FC 03: Thanh ghi D100 (Sản lượng hiện tại)
constexpr uint16_t COIL_M100_STATUS = 100; // FC 01: Bit M100 (Trạng thái máy chạy/dừng)

// Chiều ESP32 -> PLC (Ghi)
constexpr uint16_t COIL_M0_START    = 0;   // FC 05: Bit M0  (Lệnh Start)
constexpr uint16_t COIL_M1_STOP     = 1;   // FC 05: Bit M1  (Lệnh Stop)
constexpr uint16_t COIL_M10_RESET   = 10;  // FC 05: Bit M10 (Lệnh Reset bộ đếm)
constexpr uint16_t REG_D200_TARGET  = 200; // FC 06: Thanh ghi D200 (Sản lượng mục tiêu)

// ============================================================================
// 3. BIẾN TOÀN CỤC
// ============================================================================
ModbusMaster node;
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);
String       mqttClientId;

uint16_t    currentCount   = 0;
bool        machineStatus  = false;
uint16_t    targetCount    = 0;
bool        modbusOk       = false;
const char* lastModbusErr  = "Chua doc lan nao";

// Giá trị đã gửi lần trước (để chỉ publish khi có thay đổi)
uint16_t pubCount   = 0xFFFF;
bool     pubStatus  = false;
uint16_t pubTarget  = 0xFFFF;
bool     pubOk      = false;
bool     forcePublish = true;

unsigned long lastPollTime    = 0;
unsigned long lastPublishTime = 0;
unsigned long lastMqttRetry   = 0;
constexpr unsigned long POLL_INTERVAL_MS = 1000; // Chu kỳ đọc 1000ms

// ---------------------------------------------------------------------------
// Hàng đợi lệnh: callback MQTT chỉ đưa lệnh vào hàng đợi, loop() mới thực thi
// Modbus. Tránh chạy Modbus (blocking) bên trong callback của PubSubClient.
// ---------------------------------------------------------------------------
enum class CmdType : uint8_t { START, STOP, RESET, TARGET };
struct Command { CmdType type; uint16_t value; };

Command cmdQueue[CMD_QUEUE_SIZE];
uint8_t qHead = 0, qTail = 0;

bool enqueueCommand(CmdType type, uint16_t value = 0) {
  uint8_t next = (qTail + 1) % CMD_QUEUE_SIZE;
  if (next == qHead) return false;          // hàng đợi đầy
  cmdQueue[qTail] = {type, value};
  qTail = next;
  return true;
}

bool dequeueCommand(Command& c) {
  if (qHead == qTail) return false;
  c = cmdQueue[qHead];
  qHead = (qHead + 1) % CMD_QUEUE_SIZE;
  return true;
}

// ============================================================================
// 4. CÁC HÀM HỖ TRỢ GIAO TIẾP MODBUS
// ============================================================================

// Giải mã lỗi của thư viện ModbusMaster để dễ debug khi đấu dây
const char* getModbusErrorString(uint8_t result) {
  switch (result) {
    case ModbusMaster::ku8MBSuccess:             return "OK (Thanh cong)";
    case ModbusMaster::ku8MBIllegalFunction:     return "Loi 01: Sai Function Code";
    case ModbusMaster::ku8MBIllegalDataAddress:  return "Loi 02: Sai dia chi thanh ghi/coil";
    case ModbusMaster::ku8MBIllegalDataValue:    return "Loi 03: Gia tri du lieu khong hop le";
    case ModbusMaster::ku8MBSlaveDeviceFailure:  return "Loi 04: Loi thiet bi Slave (PLC)";
    case ModbusMaster::ku8MBInvalidSlaveID:      return "Loi E0: Sai Slave ID";
    case ModbusMaster::ku8MBInvalidFunction:     return "Loi E1: Function khong hop le";
    case ModbusMaster::ku8MBResponseTimedOut:    return "Loi E2: Timeout (Kiem tra day A/B, Baudrate, Parity)";
    case ModbusMaster::ku8MBInvalidCRC:          return "Loi E3: Sai ma CRC (Nhieu duong truyen/sai Parity)";
    default:                                     return "Loi khong xac dinh";
  }
}

// Kích xung Bit M (ON -> chờ 50ms -> OFF). Trả về mã kết quả Modbus.
uint8_t pulseCoil(uint16_t coilAddress, const char* cmdName) {
  Serial.printf("\n[CMD] Dang gui xung %s (Coil M%u)...\n", cmdName, coilAddress);

  uint8_t res = node.writeSingleCoil(coilAddress, 1);   // FC 05 - ON (0xFF00)
  if (res != ModbusMaster::ku8MBSuccess) {
    Serial.printf("  -> [THAT BAI khi SET ON] %s (Ma: 0x%02X)\n", getModbusErrorString(res), res);
    return res;
  }

  delay(50); // Giữ mức ON trong 50ms để PLC kịp quét qua chu kỳ Ladder

  res = node.writeSingleCoil(coilAddress, 0);           // FC 05 - OFF (0x0000)
  if (res != ModbusMaster::ku8MBSuccess) {
    Serial.printf("  -> [THAT BAI khi RESET OFF] %s (Ma: 0x%02X)\n", getModbusErrorString(res), res);
    return res;
  }

  Serial.printf("  -> [THANH CONG] Da kich xung M%u hoan tat!\n", coilAddress);
  return res;
}

// Ghi giá trị 16-bit xuống D200 - FC 06. Trả về mã kết quả Modbus.
uint8_t writeTargetD200(uint16_t value) {
  Serial.printf("\n[CMD] Dang ghi Target D200 = %u...\n", value);
  uint8_t res = node.writeSingleRegister(REG_D200_TARGET, value);
  if (res == ModbusMaster::ku8MBSuccess) {
    targetCount = value;
    Serial.printf("  -> [THANH CONG] Da cap nhat D200 = %u\n", value);
  } else {
    Serial.printf("  -> [THAT BAI] %s (Ma: 0x%02X)\n", getModbusErrorString(res), res);
  }
  return res;
}

// Đọc D100, M100, D200 từ PLC. Trả về true nếu tất cả đều đọc thành công.
bool pollPLCData() {
  uint8_t res;

  // 1. Đọc thanh ghi D100 (FC 03 - Read Holding Registers)
  res = node.readHoldingRegisters(REG_D100_COUNT, 1);
  if (res != ModbusMaster::ku8MBSuccess) {
    lastModbusErr = getModbusErrorString(res);
    Serial.printf("[POLL] Loi doc D100: %s (0x%02X)\n", lastModbusErr, res);
    return false;
  }
  currentCount = node.getResponseBuffer(0);

  delay(10); // Khoảng nghỉ ngắn giữa 2 khung truyền Modbus RTU

  // 2. Đọc trạng thái M100 (FC 01 - Read Coils)
  res = node.readCoils(COIL_M100_STATUS, 1);
  if (res != ModbusMaster::ku8MBSuccess) {
    lastModbusErr = getModbusErrorString(res);
    Serial.printf("[POLL] Loi doc M100: %s (0x%02X)\n", lastModbusErr, res);
    return false;
  }
  machineStatus = (node.getResponseBuffer(0) & 0x01) != 0;

  delay(10);

  // 3. Đọc lại D200 (FC 03) để server luôn thấy giá trị Target thật trong PLC
  //    (đúng cả khi ESP32 vừa khởi động lại hoặc D200 bị sửa từ HMI/PLC)
  res = node.readHoldingRegisters(REG_D200_TARGET, 1);
  if (res != ModbusMaster::ku8MBSuccess) {
    lastModbusErr = getModbusErrorString(res);
    Serial.printf("[POLL] Loi doc D200: %s (0x%02X)\n", lastModbusErr, res);
    return false;
  }
  targetCount = node.getResponseBuffer(0);

  lastModbusErr = "";
  digitalWrite(ONBOARD_LED, !digitalRead(ONBOARD_LED)); // LED nháy = đọc OK

  Serial.printf("[MONITOR] D100 (Count): %u | M100 (Status): %s | Target (D200): %u\n",
                currentCount,
                machineStatus ? "RUNNING (1)" : "STOPPED (0)",
                targetCount);
  return true;
}

// ============================================================================
// 5. MQTT
// ============================================================================

// Gửi dữ liệu D100 / M100 lên server (JSON, retained)
void publishData() {
  if (!mqtt.connected()) return;

  JsonDocument doc;
  doc["count"]     = currentCount;     // D100
  doc["running"]   = machineStatus;    // M100
  doc["target"]    = targetCount;      // D200 (đọc từ PLC)
  doc["modbus_ok"] = modbusOk;
  if (!modbusOk) doc["error"] = lastModbusErr;
  doc["rssi"]      = WiFi.RSSI();
  doc["uptime_s"]  = millis() / 1000;

  char buf[256];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  if (mqtt.publish(TOPIC_DATA, (const uint8_t*)buf, n, true)) {
    pubCount = currentCount;
    pubStatus = machineStatus;
    pubTarget = targetCount;
    pubOk = modbusOk;
    lastPublishTime = millis();
    forcePublish = false;
  }
}

// Gửi phản hồi kết quả thực thi lệnh về server
void publishAck(const char* cmdName, uint8_t res, uint16_t value) {
  if (!mqtt.connected()) return;

  JsonDocument doc;
  doc["cmd"] = cmdName;
  if (strcmp(cmdName, "target") == 0) doc["value"] = value;
  doc["ok"]  = (res == ModbusMaster::ku8MBSuccess);
  if (res != ModbusMaster::ku8MBSuccess) doc["error"] = getModbusErrorString(res);

  char buf[192];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  mqtt.publish(TOPIC_ACK, (const uint8_t*)buf, n, false);
}

// Nhận lệnh từ server. Chấp nhận:
//   JSON : {"cmd":"start"} | {"cmd":"stop"} | {"cmd":"reset"} | {"cmd":"target","value":500}
//   Text : start | stop | reset
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String cmd;
  long value = 0;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (!err && doc.is<JsonObject>()) {
    cmd   = doc["cmd"] | "";
    value = doc["value"] | 0L;
  } else {
    cmd.reserve(length);
    for (unsigned int i = 0; i < length; i++) cmd += (char)payload[i];
  }
  cmd.trim();
  cmd.toLowerCase();

  Serial.printf("[MQTT] Nhan [%s]: cmd=%s value=%ld\n", topic, cmd.c_str(), value);

  bool queued = false;
  if      (cmd == "start") queued = enqueueCommand(CmdType::START);
  else if (cmd == "stop")  queued = enqueueCommand(CmdType::STOP);
  else if (cmd == "reset") queued = enqueueCommand(CmdType::RESET);
  else if (cmd == "target") {
    if (value > 0 && value <= 65535) {
      queued = enqueueCommand(CmdType::TARGET, (uint16_t)value);
    } else {
      Serial.println("[MQTT] Gia tri target khong hop le (1..65535)");
      return;
    }
  } else {
    Serial.println("[MQTT] Lenh khong hop le, bo qua");
    return;
  }

  if (!queued) Serial.println("[MQTT] Hang doi lenh day, bo qua lenh");
}

// Kết nối lại MQTT (không chặn loop quá lâu: chỉ thử mỗi MQTT_RETRY_MS)
void maintainMqtt() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) { mqtt.loop(); return; }

  unsigned long now = millis();
  if (now - lastMqttRetry < MQTT_RETRY_MS) return;
  lastMqttRetry = now;

  Serial.printf("[MQTT] Dang ket noi %s:%u ...\n", MQTT_HOST, MQTT_PORT);
  bool ok = mqtt.connect(mqttClientId.c_str(),
                         MQTT_USER[0] ? MQTT_USER : nullptr,
                         MQTT_PASS[0] ? MQTT_PASS : nullptr,
                         TOPIC_STATUS, 1, true, "offline");   // LWT
  if (ok) {
    Serial.println("[MQTT] Da ket noi!");
    mqtt.publish(TOPIC_STATUS, "online", true);
    mqtt.subscribe(TOPIC_CMD, 1);
    forcePublish = true;              // gửi ngay trạng thái hiện tại
  } else {
    Serial.printf("[MQTT] That bai, state=%d. Thu lai sau %lus\n",
                  mqtt.state(), MQTT_RETRY_MS / 1000);
  }
}

// ============================================================================
// 6. THỰC THI LỆNH (từ MQTT hoặc Serial)
// ============================================================================
void executeCommand(const Command& c) {
  const char* name = "";
  uint8_t res = ModbusMaster::ku8MBSuccess;

  switch (c.type) {
    case CmdType::START:  name = "start";  res = pulseCoil(COIL_M0_START,  "START"); break;
    case CmdType::STOP:   name = "stop";   res = pulseCoil(COIL_M1_STOP,   "STOP");  break;
    case CmdType::RESET:  name = "reset";  res = pulseCoil(COIL_M10_RESET, "RESET"); break;
    case CmdType::TARGET: name = "target"; res = writeTargetD200(c.value);           break;
  }

  publishAck(name, res, c.value);

  // Đọc lại PLC ngay để server thấy trạng thái mới
  lastPollTime = millis() - POLL_INTERVAL_MS;
  forcePublish = true;
}

// Lệnh test từ Serial Monitor (đưa vào cùng hàng đợi với lệnh MQTT)
void handleSerialCommands() {
  if (!Serial.available()) return;

  String input = Serial.readStringUntil('\n');
  input.trim();
  if (input.length() == 0) return;

  char cmd = tolower(input.charAt(0));
  switch (cmd) {
    case 's': enqueueCommand(CmdType::START); break;
    case 'p': enqueueCommand(CmdType::STOP);  break;
    case 'r': enqueueCommand(CmdType::RESET); break;
    case 't': {
      String valStr = input.substring(1);
      valStr.trim();
      int val = valStr.toInt();
      if (val > 0 && val <= 65535) enqueueCommand(CmdType::TARGET, (uint16_t)val);
      else Serial.println("[WARN] Gia tri Target phai > 0 (VD: go 't500' roi Enter)");
      break;
    }
    default:
      Serial.println("[HELP] Lenh hop le: 's' (Start M0) | 'p' (Stop M1) | 'r' (Reset M10) | 't<so>' (Ghi D200, VD: t500)");
      break;
  }
}

// ============================================================================
// 7. SETUP & LOOP
// ============================================================================
void setup() {
  pinMode(ONBOARD_LED, OUTPUT);
  Serial.begin(115200);
  delay(1000);

  // UART2 cho RS485 (RX = 16, TX = 17)
  Serial2.begin(MODBUS_BAUD, MODBUS_CONFIG, MODBUS_RX_PIN, MODBUS_TX_PIN);
  node.begin(PLC_SLAVE_ID, Serial2);

  // WiFi: không chờ ở đây, để Modbus chạy ngay cả khi chưa có mạng
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // MQTT
  mqttClientId = "esp32-xinje-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(30);   // > timeout Modbus (2s) để không bị broker ngắt

  Serial.println("\n=======================================================");
  Serial.println("   ESP32 MODBUS RTU <-> MQTT GATEWAY - PLC XINJE XD2   ");
  Serial.println("=======================================================");
  Serial.printf ("MQTT client ID : %s\n", mqttClientId.c_str());
  Serial.printf ("Publish data   : %s\n", TOPIC_DATA);
  Serial.printf ("Subscribe cmd  : %s\n", TOPIC_CMD);
  Serial.println("Lenh Serial: 's' START | 'p' STOP | 'r' RESET | 't500' TARGET");
  Serial.println("=======================================================\n");
}

void loop() {
  // 1. Giữ kết nối MQTT + nhận lệnh từ server
  static bool wifiWasConnected = false;
  bool wifiNow = (WiFi.status() == WL_CONNECTED);
  if (wifiNow != wifiWasConnected) {
    wifiWasConnected = wifiNow;
    if (wifiNow) Serial.printf("[WIFI] Da ket noi, IP: %s\n", WiFi.localIP().toString().c_str());
    else         Serial.println("[WIFI] Mat ket noi, dang thu lai...");
  }
  maintainMqtt();

  // 2. Lệnh từ Serial Monitor
  handleSerialCommands();

  // 3. Thực thi lệnh trong hàng đợi (ESP32 -> PLC)
  Command c;
  while (dequeueCommand(c)) {
    executeCommand(c);
    mqtt.loop();
  }

  // 4. Đọc PLC định kỳ (PLC -> ESP32)
  unsigned long now = millis();
  if (now - lastPollTime >= POLL_INTERVAL_MS) {
    lastPollTime = now;
    modbusOk = pollPLCData();
  }

  // 5. Gửi lên server: khi có thay đổi, hoặc định kỳ HEARTBEAT_MS
  bool changed = (currentCount != pubCount) || (machineStatus != pubStatus) ||
                 (targetCount != pubTarget) || (modbusOk != pubOk);
  if (forcePublish || changed || (millis() - lastPublishTime >= HEARTBEAT_MS)) {
    publishData();
  }
}