#include <Arduino.h>
#include <ModbusMaster.h>
#include <driver/gpio.h>

// ============================================================================
// 1. CẤU HÌNH PHẦN CỨNG & THÔNG SỐ MODBUS RTU (PLC XINJE XD2-16T-E)
// ============================================================================
constexpr uint8_t ONBOARD_LED   = 2;
constexpr uint8_t MODBUS_RX_PIN = 16;   // Nối vào chân RXD/RO của mạch MAX485
constexpr uint8_t MODBUS_TX_PIN = 17;   // Nối vào chân TXD/DI của mạch MAX485

uint8_t  plcSlaveId   = 2;              // Slave ID đang đặt trên PLC
uint32_t modbusBaud   = 19200;          // Mặc định hãng Xinje: 19200
uint32_t modbusConfig = SERIAL_8E1;     // Mặc định hãng Xinje: Even Parity (8E1)

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

uint16_t currentCount  = 0;
bool     machineStatus = false;
uint16_t targetCount   = 0;

unsigned long lastPollTime = 0;
constexpr unsigned long POLL_INTERVAL_MS = 1000; // Chu kỳ đọc 1000ms

// ============================================================================
// 4. CÁC HÀM HỖ TRỢ GIAO TIẾP MODBUS
// ============================================================================

// Callback chạy ngay sau khi ESP32 vừa phát xong khung truyền (TX)
// Chờ 1ms (1000us) cho byte tự vọng cuối cùng (0xFF) dịch xong vào FIFO rồi xóa sạch
// (Trên PLC cài Send Delay Time = 3ms nên chờ 1ms hoàn toàn không làm mất byte nào của PLC)
void postTransmissionCallback() {
  delayMicroseconds(1000);
  while (Serial2.available()) {
    Serial2.read();
  }
}

const char* getModbusErrorString(uint8_t result) {
  switch (result) {
    case ModbusMaster::ku8MBSuccess:             return "OK (Thanh cong)";
    case ModbusMaster::ku8MBIllegalFunction:     return "Loi 01: Sai Function Code";
    case ModbusMaster::ku8MBIllegalDataAddress:  return "Loi 02: Sai dia chi thanh ghi/coil";
    case ModbusMaster::ku8MBIllegalDataValue:    return "Loi 03: Gia tri du lieu khong hop le";
    case ModbusMaster::ku8MBSlaveDeviceFailure:  return "Loi 04: Loi thiet bi Slave (PLC)";
    case ModbusMaster::ku8MBInvalidSlaveID:      return "Loi E0: Sai Slave ID";
    case ModbusMaster::ku8MBInvalidFunction:     return "Loi E1: Function khong hop le";
    case ModbusMaster::ku8MBResponseTimedOut:    return "Loi E2: Timeout (PLC chua phan hoi)";
    case ModbusMaster::ku8MBInvalidCRC:          return "Loi E3: Sai ma CRC (Nhieu duong truyen/sai Parity)";
    default:                                     return "Loi khong xac dinh";
  }
}

// Hàm tính CRC16 chuẩn Modbus RTU
uint16_t calcModbusCRC(const uint8_t* buf, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (uint8_t pos = 0; pos < len; pos++) {
    crc ^= (uint16_t)buf[pos];
    for (int i = 8; i != 0; i--) {
      if ((crc & 0x0001) != 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

// Hàm gửi thử lệnh đọc D100 và chỉ thu các byte PLC phản hồi SAU KHI phát xong
void debugRawModbusReadD100(uint8_t testSlaveId) {
  uint8_t frame[8] = {
    testSlaveId, 0x03,
    (uint8_t)(REG_D100_COUNT >> 8), (uint8_t)(REG_D100_COUNT & 0xFF),
    0x00, 0x01, 0x00, 0x00
  };
  uint16_t crc = calcModbusCRC(frame, 6);
  frame[6] = crc & 0xFF;        // CRC Low
  frame[7] = (crc >> 8) & 0xFF; // CRC High

  delay(20);
  while (Serial2.available()) Serial2.read();

  // Gửi khung hỏi xuống PLC
  Serial2.write(frame, 8);
  Serial2.flush();

  // Đợi 1ms cho byte tự vọng cuối cùng (FF) vào hết bộ đệm rồi xóa sạch
  delayMicroseconds(1000);
  while (Serial2.available()) Serial2.read();

  // Chờ 100ms cho PLC phản hồi thật sự
  delay(100);

  int count = Serial2.available();
  Serial.printf("  [RAW TEST ID=%u] TX: %02X %02X %02X %02X %02X %02X %02X %02X -> RX that (%d bytes): ",
                testSlaveId,
                frame[0], frame[1], frame[2], frame[3], frame[4], frame[5], frame[6], frame[7],
                count);
  while (Serial2.available()) {
    uint8_t b = Serial2.read();
    Serial.printf("%02X ", b);
  }
  Serial.println();
}

// Hàm tự động quét tìm thông số PLC (Baudrate, Parity, Slave ID)
void autoScanPLC() {
  Serial.println("\n================ BAT DAU DO TIM CAU HINH PLC ================");
  const uint32_t bauds[]   = {19200, 9600, 38400, 115200};
  const uint32_t configs[] = {SERIAL_8E1, SERIAL_8N1, SERIAL_8O1};
  const char* configNames[] = {"8E1 (Even)", "8N1 (None)", "8O1 (Odd)"};
  const uint8_t ids[]      = {1, 2};

  for (uint8_t bIdx = 0; bIdx < 4; bIdx++) {
    for (uint8_t cIdx = 0; cIdx < 3; cIdx++) {
      Serial2.end();
      delay(20);
      Serial2.begin(bauds[bIdx], configs[cIdx], MODBUS_RX_PIN, MODBUS_TX_PIN);
      gpio_pullup_en((gpio_num_t)MODBUS_RX_PIN);
      delay(20);

      for (uint8_t iIdx = 0; iIdx < 2; iIdx++) {
        uint8_t testId = ids[iIdx];
        node.begin(testId, Serial2);
        node.postTransmission(postTransmissionCallback);

        Serial.printf("Thu: Baud=%u | Parity=%s | ID=%u ... ",
                      bauds[bIdx], configNames[cIdx], testId);

        uint8_t res = node.readHoldingRegisters(REG_D100_COUNT, 1);
        if (res == ModbusMaster::ku8MBSuccess) {
          plcSlaveId   = testId;
          modbusBaud   = bauds[bIdx];
          modbusConfig = configs[cIdx];
          Serial.printf("THANH CONG! (D100 = %u)\n", node.getResponseBuffer(0));
          Serial.println("=============================================================\n");
          return;
        } else {
          Serial.printf("Loi 0x%02X\n", res);
        }
        delay(30);
      }
    }
  }

  // Khôi phục lại cấu hình mặc định nếu chưa tìm thấy
  Serial2.end();
  Serial2.begin(modbusBaud, modbusConfig, MODBUS_RX_PIN, MODBUS_TX_PIN);
  gpio_pullup_en((gpio_num_t)MODBUS_RX_PIN);
  node.begin(plcSlaveId, Serial2);
  node.postTransmission(postTransmissionCallback);
  Serial.println("-> Chua tim thay phan hoi tu PLC. Quay ve Baud=19200, 8E1, ID=2");
  Serial.println("=============================================================\n");
}

// Hàm kích xung Bit M (Bật ON -> chờ 50ms -> tắt OFF) như nút nhấn nhả
bool pulseCoil(uint16_t coilAddress, const char* cmdName) {
  Serial.printf("\n[CMD] Dang gui xung %s (Coil M%u)...\n", cmdName, coilAddress);

  uint8_t res = node.writeSingleCoil(coilAddress, 1);
  if (res != ModbusMaster::ku8MBSuccess) {
    Serial.printf("  -> [THAT BAI khi SET ON] %s (Ma: 0x%02X)\n", getModbusErrorString(res), res);
    return false;
  }

  delay(50);

  res = node.writeSingleCoil(coilAddress, 0);
  if (res != ModbusMaster::ku8MBSuccess) {
    Serial.printf("  -> [THAT BAI khi RESET OFF] %s (Ma: 0x%02X)\n", getModbusErrorString(res), res);
    return false;
  }

  Serial.printf("  -> [THANH CONG] Da kich xung M%u hoan tat!\n", coilAddress);
  return true;
}

// Hàm ghi giá trị 16-bit xuống thanh ghi D - FC 06
bool writeTargetD200(uint16_t value) {
  Serial.printf("\n[CMD] Dang ghi Target D200 = %u...\n", value);
  uint8_t res = node.writeSingleRegister(REG_D200_TARGET, value);
  if (res == ModbusMaster::ku8MBSuccess) {
    targetCount = value;
    Serial.printf("  -> [THANH CONG] Da cap nhat D200 = %u\n", value);
    return true;
  } else {
    Serial.printf("  -> [THAT BAI] %s (Ma: 0x%02X)\n", getModbusErrorString(res), res);
    return false;
  }
}

// Hàm đọc dữ liệu định kỳ từ PLC (D100 và M100)
void pollPLCData() {
  uint8_t res;

  // 1. Đọc thanh ghi D100 (FC 03 - Read Holding Registers)
  res = node.readHoldingRegisters(REG_D100_COUNT, 1);
  if (res == ModbusMaster::ku8MBSuccess) {
    currentCount = node.getResponseBuffer(0);
  } else {
    Serial.printf("[POLL] Loi doc D100 (ID=%u): %s (0x%02X)\n", plcSlaveId, getModbusErrorString(res), res);
    debugRawModbusReadD100(plcSlaveId);
    if (plcSlaveId != 1) {
      debugRawModbusReadD100(1);
    }
    return;
  }

  delay(10);

  // 2. Đọc trạng thái M100 (FC 01 - Read Coils)
  res = node.readCoils(COIL_M100_STATUS, 1);
  if (res == ModbusMaster::ku8MBSuccess) {
    machineStatus = (node.getResponseBuffer(0) & 0x01) != 0;
  } else {
    Serial.printf("[POLL] Loi doc M100: %s (0x%02X)\n", getModbusErrorString(res), res);
    return;
  }

  digitalWrite(ONBOARD_LED, !digitalRead(ONBOARD_LED));

  Serial.printf("[MONITOR] D100 (Count): %u | M100 (Status): %s | Target (D200): %u\n",
                currentCount,
                machineStatus ? "RUNNING (1)" : "STOPPED (0)",
                targetCount);
}

// Xử lý lệnh test nhập từ bàn phím qua Serial Monitor
void handleSerialCommands() {
  if (!Serial.available()) return;

  String input = Serial.readStringUntil('\n');
  input.trim();
  if (input.length() == 0) return;

  char cmd = tolower(input.charAt(0));
  switch (cmd) {
    case 'a': // Auto-scan PLC
      autoScanPLC();
      break;
    case 's': // Start -> M0
      pulseCoil(COIL_M0_START, "START");
      break;
    case 'p': // Stop -> M1
      pulseCoil(COIL_M1_STOP, "STOP");
      break;
    case 'r': // Reset -> M10
      pulseCoil(COIL_M10_RESET, "RESET");
      break;
    case 't': { // Set Target -> D200 (VD: gõ t500)
      String valStr = input.substring(1);
      valStr.trim();
      int val = valStr.toInt();
      if (val > 0 && val <= 65535) {
        writeTargetD200((uint16_t)val);
      } else {
        Serial.println("[WARN] Gia tri Target phai > 0 (VD: go 't500' roi Enter)");
      }
      break;
    }
    default:
      Serial.println("[HELP] Lenh: 'a' (Do tim PLC) | 's' (Start) | 'p' (Stop) | 'r' (Reset) | 't500' (Set D200)");
      break;
  }
}

// ============================================================================
// 5. SETUP & LOOP
// ============================================================================
void setup() {
  pinMode(ONBOARD_LED, OUTPUT);
  Serial.begin(115200);
  delay(1000);

  // Khởi tạo UART2 cho giao tiếp RS485 (RX = 16, TX = 17)
  Serial2.begin(modbusBaud, modbusConfig, MODBUS_RX_PIN, MODBUS_TX_PIN);
  // Bật trở kéo lên phần cứng (Hardware Pull-up) mà không làm ngắt UART2
  gpio_pullup_en((gpio_num_t)MODBUS_RX_PIN);

  node.begin(plcSlaveId, Serial2);
  node.postTransmission(postTransmissionCallback);

  Serial.println("\n=======================================================");
  Serial.println("   ESP32 MODBUS RTU MASTER - TEST VOI PLC XINJE XD2    ");
  Serial.println("=======================================================");
  Serial.println("Huong dan lenh test tren Serial Monitor:");
  Serial.println("  - Go 'a' + Enter : Tu dong quet Baudrate / Parity / ID");
  Serial.println("  - Go 's' + Enter : Kich M0  (START)");
  Serial.println("  - Go 'p' + Enter : Kich M1  (STOP)");
  Serial.println("  - Go 'r' + Enter : Kich M10 (RESET)");
  Serial.println("  - Go 't500'      : Ghi D200 = 500 (SET TARGET)");
  Serial.println("=======================================================\n");
}

void loop() {
  handleSerialCommands();

  unsigned long now = millis();
  if (now - lastPollTime >= POLL_INTERVAL_MS) {
    lastPollTime = now;
    pollPLCData();
  }
}
