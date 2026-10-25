#include <Arduino.h>
#include <ModbusMaster.h>

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

uint16_t currentCount  = 0;
bool     machineStatus = false;
uint16_t targetCount   = 0;

unsigned long lastPollTime = 0;
constexpr unsigned long POLL_INTERVAL_MS = 1000; // Chu kỳ đọc 1000ms

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

// Hàm kích xung Bit M (Bật ON -> chờ 50ms -> tắt OFF) như nút nhấn nhả
bool pulseCoil(uint16_t coilAddress, const char* cmdName) {
  Serial.printf("\n[CMD] Dang gui xung %s (Coil M%u)...\n", cmdName, coilAddress);

  // Bước 1: Ghi ON (0xFF00) - FC 05
  uint8_t res = node.writeSingleCoil(coilAddress, 1);
  if (res != ModbusMaster::ku8MBSuccess) {
    Serial.printf("  -> [THAT BAI khi SET ON] %s (Ma: 0x%02X)\n", getModbusErrorString(res), res);
    return false;
  }

  delay(50); // Giữ mức ON trong 50ms để PLC kịp quét qua chu kỳ Ladder

  // Bước 2: Nhả về OFF (0x0000) - FC 05
  res = node.writeSingleCoil(coilAddress, 0);
  if (res != ModbusMaster::ku8MBSuccess) {
    Serial.printf("  -> [THAT BAI khi RESET OFF] %s (Ma: 0x%02X)\n", getModbusErrorString(res), res);
    return false;
  }

  Serial.printf("  -> [THANH CONG] Da kich xung M%u hoàn tất!\n", coilAddress);
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
    Serial.printf("[POLL] Loi doc D100: %s (0x%02X)\n", getModbusErrorString(res), res);
    return;
  }

  delay(10); // Khoảng nghỉ ngắn (Silent Interval) giữa 2 khung truyền Modbus RTU

  // 2. Đọc trạng thái M100 (FC 01 - Read Coils)
  res = node.readCoils(COIL_M100_STATUS, 1);
  if (res == ModbusMaster::ku8MBSuccess) {
    machineStatus = (node.getResponseBuffer(0) & 0x01) != 0;
  } else {
    Serial.printf("[POLL] Loi doc M100: %s (0x%02X)\n", getModbusErrorString(res), res);
    return;
  }

  // Đảo trạng thái đèn LED onboard báo hiệu đọc thành công
  digitalWrite(ONBOARD_LED, !digitalRead(ONBOARD_LED));

  // In kết quả giám sát
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
      Serial.println("[HELP] Lenh hop le: 's' (Start M0) | 'p' (Stop M1) | 'r' (Reset M10) | 't<so>' (Ghi D200, VD: t500)");
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
  Serial2.begin(MODBUS_BAUD, MODBUS_CONFIG, MODBUS_RX_PIN, MODBUS_TX_PIN);

  // Khởi tạo Modbus Master với Slave ID = 1 trên cổng Serial2
  node.begin(PLC_SLAVE_ID, Serial2);

  Serial.println("\n=======================================================");
  Serial.println("   ESP32 MODBUS RTU MASTER - TEST VOI PLC XINJE XD2    ");
  Serial.println("=======================================================");
  Serial.println("Huong dan lenh test tren Serial Monitor:");
  Serial.println("  - Go 's' + Enter : Kich M0  (START)");
  Serial.println("  - Go 'p' + Enter : Kich M1  (STOP)");
  Serial.println("  - Go 'r' + Enter : Kich M10 (RESET)");
  Serial.println("  - Go 't500'      : Ghi D200 = 500 (SET TARGET)");
  Serial.println("=======================================================\n");
}

void loop() {
  // 1. Lắng nghe lệnh điều khiển từ Serial Monitor (Downstream: ESP32 -> PLC)
  handleSerialCommands();

  // 2. Đọc dữ liệu định kỳ mỗi 1000ms (Upstream: PLC -> ESP32)
  unsigned long now = millis();
  if (now - lastPollTime >= POLL_INTERVAL_MS) {
    lastPollTime = now;
    pollPLCData();
  }
}