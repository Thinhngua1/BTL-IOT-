# CẨM NANG NHẬP MÔN & BẮT ĐẦU LẬP TRÌNH ESP32 VỚI VS CODE + PLATFORMIO

Tài liệu ghi chú nhanh (Cheat Sheet) toàn bộ các bước thiết lập môi trường, xử lý cổng nạp, tạo dự án và quy trình nạp code cơ bản cho vi điều khiển ESP32.

---

## 1. XỬ LÝ DRIVER & CỔNG COM TRÊN WINDOWS (XỬ LÝ DẤU `!`)

Khi cắm cáp Micro-USB / Type-C nối ESP32 vào máy tính, nếu mở **Device Manager** $\rightarrow$ **Other devices** thấy thiết bị có dấu chấm than vàng `(!)` nghĩa là Windows chưa có driver cho chip nạp USB-UART.

### Cách xác định chip nạp và cài đặt:
1. Nhìn con chip vuông nhỏ nằm sát cổng USB trên bo mạch ESP32:
   * **Nếu là chip CP2102 (Silicon Labs):** Tải bộ cài `CP210x Universal Windows Driver`, giải nén file `.zip`, click chuột phải vào file `silabser.inf` chọn **Install**.
   * **Nếu là chip CH340 / CH340G / CH340C (WCH):** Tải file `CH341SER.EXE`, mở lên và bấm nút **INSTALL**.
2. **Kiểm tra thành công:**
   * Rút cáp cắm lại. Dấu chấm than vàng sẽ biến mất.
   * Tại mục **Ports (COM & LPT)** sẽ hiện:  
     `Silicon Labs CP210x USB to UART Bridge (COMx)` hoặc `USB-SERIAL CH340 (COMx)`.
   * Ghi nhớ số cổng **COMx** này.

---

## 2. THIẾT LẬP MÔI TRƯỜNG VS CODE + PLATFORMIO

1. Cài đặt **VS Code** (Visual Studio Code).
2. Mở VS Code $\rightarrow$ Nhấn tổ hợp phím `Ctrl + Shift + X` (Extensions) $\rightarrow$ Tìm **`PlatformIO IDE`** $\rightarrow$ Bấm **Install**.
3. Khởi động lại VS Code sau khi cài đặt xong (sẽ xuất hiện biểu tượng **đầu con kiến 🐜** ở thanh bên trái).

---

## 3. QUY TRÌNH TẠO DỰ ÁN MỚI (PROJECT WORKFLOW)

1. Bấm vào biểu tượng **Con kiến 🐜 (PlatformIO)** $\rightarrow$ Chọn **Open** $\rightarrow$ Bấm nút **New Project**.
2. Điền thông số:
   * **Name:** Tên dự án (Ví dụ: `ESP32_Blink_Test`).
   * **Board:** Chọn `Espressif ESP32 Dev Module` (dùng chung cho mọi dòng ESP32 WROOM 30 chân / 38 chân).
   * **Framework:** Chọn `Arduino`.
   * **Location:** Bỏ tick *Use default location* nếu muốn chọn thư mục làm việc theo ý muốn.
3. Bấm **Finish** và đợi PlatformIO khởi tạo cấu trúc thư mục.

---

## 4. CẤU TRÚC FILE DỰ ÁN & VỊ TRÍ VIẾT CODE

```text
ESP32_Project/
├── .pio/               <-- Thư mục chứa file build biên dịch (không sửa)
├── include/            <-- Thư mục chứa các file khai báo .h
├── lib/                <-- Thư mục chứa các thư viện riêng
├── src/
│   └── main.cpp        <-- ⭐️ FILE CỐT LÕI: Nơi viết toàn bộ mã nguồn C++
└── platformio.ini      <-- ⭐️ FILE CẤU HÌNH: Cấu hình bo mạch, tốc độ Serial, thư viện
```

### Cấu hình `platformio.ini` chuẩn:
Mở file `platformio.ini` và thêm dòng cấu hình tốc độ màn hình Serial `monitor_speed = 115200`:
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
```

---

## 5. MÃ NGUỒN MẪU BLINK LED & SERIAL MONITOR (`src/main.cpp`)

Hầu hết board ESP32-WROOM đều tích hợp sẵn 1 đèn LED màu xanh dương gắn tại chân **GPIO 2**:

```cpp
#include <Arduino.h>

// Chân GPIO 2 nối LED tích hợp trên bo mạch
constexpr uint8_t ONBOARD_LED = 2;

void setup() {
  // Cấu hình chân LED làm đầu ra
  pinMode(ONBOARD_LED, OUTPUT);

  // Mở cổng Serial USB tốc độ 115200 baud
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=================================");
  Serial.println("  HELLO WORLD TU ESP32 WROOM!   ");
  Serial.println("=================================");
}

void loop() {
  // Bật đèn LED + In thông báo
  digitalWrite(ONBOARD_LED, HIGH);
  Serial.println("-> LED: ON (Bat)");
  delay(1000); // Đợi 1 giây

  // Tắt đèn LED + In thông báo
  digitalWrite(ONBOARD_LED, LOW);
  Serial.println("-> LED: OFF (Tat)");
  delay(1000); // Đợi 1 giây
}
```

---

## 6. THAO TÁC NẠP CODE & MỞ SERIAL MONITOR

### Cách 1: Dùng thanh công cụ PlatformIO (Con kiến 🐜)
* Nhìn sang cột bên trái, bấm vào biểu tượng **Con kiến 🐜**.
* Vào mục `PROJECT TASKS` $\rightarrow$ `esp32dev` (hoặc `Default`) $\rightarrow$ `General`:
  * Bấm **`Upload`**: Biên dịch và nạp firmware vào ESP32.
  * Bấm **`Monitor`**: Mở cửa sổ Serial Monitor xem dữ liệu in ra.
  * Bấm **`Upload and Monitor`**: Nạp xong tự động mở màn hình giám sát.

### Cách 2: Dùng phím tắt nhanh (Command Palette)
* Nhấn `Ctrl + Shift + P` $\rightarrow$ Gõ `PlatformIO: Upload` $\rightarrow$ Nhấn Enter.
* Nhấn `Ctrl + Shift + P` $\rightarrow$ Gõ `PlatformIO: Monitor` $\rightarrow$ Nhấn Enter.

---

## 7. MẸO XỬ LÝ LỖI PHỔ BIẾN (TROUBLESHOOTING)

1. **Lỗi `Connecting........_____.....` khi nạp code:**
   * Mạch ESP32 thông thường sẽ tự động reset và nạp.
   * Nếu mạch nào không tự động vào chế độ nạp: Khi Terminal bắt đầu hiện chữ `Connecting...`, hãy **nhấn và giữ nút `BOOT`** trên mạch ESP32 khoảng 1–2 giây cho đến khi thấy chữ `Writing at 0x... (%)` thì thả tay ra.
2. **Lỗi chữ loằng ngoằng trong Serial Monitor (Ký tự rác `⸮⸮⸮`):**
   * Do lệch tốc độ Baud giữa code (`Serial.begin(115200)`) và cấu hình monitor.
   * Khắc phục: Đảm bảo trong `platformio.ini` đã có dòng `monitor_speed = 115200`.
3. **Lỗi `Failed to open COM port / Access is denied`:**
   * Do một phần mềm khác (như Arduino IDE, TeraTerm, hoặc một tab Monitor cũ) đang chiếm cổng COM.
   * Khắc phục: Đóng các phần mềm đang kết nối cổng COM hoặc rút cáp USB cắm lại.
