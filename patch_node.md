# PATCH NOTE & HANDOVER DOCUMENT (BTL IOT & ỨNG DỤNG)

**Cập nhật lần cuối:** 25/10/2026  
**Trạng thái tầng vật lý:** **ĐÃ THÔNG SUỐT 100% (RESOLVED)**  
**Dự án:** `D:\Tam\BTL_IOT_UD\BTL-IOT-`

---

## 1. TỔNG QUAN HỆ THỐNG VÀ THÔNG SỐ KỸ THUẬT

### 1.1. Thiết bị phần cứng
1. **PLC (Modbus RTU Slave):** **Xinje XD2-16T-E**
   - **COM1 (Cổng tròn Mini-DIN 8 chân):** Cắm cáp `USB-XC` nối PC để nạp chương trình và giám sát qua phần mềm `XDPPro V3.7`.
   - **COM2 (2 chân domino `A` và `B`):** Cổng RS485 nối với module RS485 của ESP32.
2. **Vi điều khiển (Modbus RTU Master):** **ESP32-DEVKITC-32UE (38 chân)**
   - Sử dụng bộ ngoại vi phần cứng **`UART2` (`Serial2`)**:
     - `GPIO 16` (`RX2`) $\rightarrow$ Nối chân `RXD` của module RS485.
     - `GPIO 17` (`TX2`) $\rightarrow$ Nối chân `TXD` của module RS485.
     - `3V3` (hoặc `5V` góc dưới bên trái dưới chân `CMD`) $\rightarrow$ `VCC` của module RS485.
     - `GND` $\rightarrow$ `GND` của module RS485.
   - *(Lưu ý: Tuyệt đối không dùng 2 chân in chữ `TX` (GPIO 1) và `RX` (GPIO 3) trên ESP32 vì hai chân này nối cứng với chip nạp USB CH340/CP2102 của mạch nạp máy tính).*
3. **Module RS485:**
   - Module TTL-to-RS485 tự động đảo chiều (XY-017) với cặp IC `74HC04D` + `MAX485ESA`.
   - Hai chân `A+` và `B-` nối tương ứng vào cọc `A` và `B` của PLC.

### 1.2. Bảng ánh xạ Modbus RTU (0-based, chuẩn Xinje XD Series)

| Hướng dữ liệu | Thiết bị PLC | Loại Modbus | Function Code | Địa chỉ Dec | Địa chỉ Hex | Ý nghĩa chức năng |
|---|---|---|---|---|---|---|
| **PLC $\rightarrow$ ESP32** | Thanh ghi `D100` | Holding Register | `FC 03` | `100` | `0x0064` | Sản lượng đếm thực tế (`currentCount`) |
| **PLC $\rightarrow$ ESP32** | Bit `M100` | Coil | `FC 01` | `100` | `0x0064` | Trạng thái máy chạy/dừng (`Y0 -> M100`) |
| **ESP32 $\rightarrow$ PLC** | Bit `M0` | Single Coil | `FC 05` | `0` | `0x0000` | Lệnh kích xung `START` (ON 50ms $\rightarrow$ OFF) |
| **ESP32 $\rightarrow$ PLC** | Bit `M1` | Single Coil | `FC 05` | `1` | `0x0001` | Lệnh kích xung `STOP` (ON 50ms $\rightarrow$ OFF) |
| **ESP32 $\rightarrow$ PLC** | Bit `M10` | Single Coil | `FC 05` | `10` | `0x000A` | Lệnh kích xung `RESET` bộ đếm |
| **ESP32 $\rightarrow$ PLC** | Thanh ghi `D200` | Single Register | `FC 06` | `200` | `0x00C8` | Đặt sản lượng mục tiêu (`targetCount`) |

---

## 2. NGUYÊN NHÂN GỐC RỄ CỦA LỖI VÀ LỜI GIẢI ĐÃ XÁC MINH

### 2.1. Hiện tượng ban đầu
- ESP32 liên tục báo lỗi `Lỗi E2: Timeout (0xE2)`.
- Khi dùng USB-RS485 trên PC nghe lén bus (`tools/modbus_test.py`), khung hỏi từ ESP32 biến thành toàn các chuỗi byte `0xFF`:
  - Khung `ID=2` biến thành đúng **17 byte `FF`**.
  - Khung `ID=1` biến thành đúng **19 byte `FF`**.
  - Khi cắm chung nhánh ESP32 vào bus lúc PLC phản hồi `D100 = 50`, khung trả lời bị chèn đè biến dạng từ 7 byte thành 26 byte (`02 A0 02 00 32 7D 91` + 19 byte `FF`). Khi tháo nhánh ESP32 thì PLC trả lời chuẩn sạch 7 byte (`02 03 02 00 32 7D 91`).

### 2.2. Nguyên nhân gốc rễ (Root Cause)
- **Dây cắm jumper nối tín hiệu (dây `TXD` / `GND`) giữa ESP32 và module RS485 bị ĐỨT NGẦM / HỞ MẠCH VẬT LÝ:**
  - Do mạch hở, dòng điện một chiều (DC) không thể truyền qua để giữ mức logic `0` trong suốt thời gian của các bit dữ liệu.
  - Tuy nhiên, khe hở cơ học siêu nhỏ tạo thành một **tụ điện ký sinh (ghép điện dung)**. Mỗi khi chân `TX` của ESP32 chuyển trạng thái từ `1 -> 0` (sườn xuống), tụ ký sinh này truyền qua một xung gai vi phân cực nhọn (~vài chục $\mu s$).
  - Bộ thu RS485 nhận mỗi xung gai đó như một bit Start giả, theo sau là đường dây bị thả nổi về mức `1` $\rightarrow$ giải mã ra byte `11111111` (`0xFF`).
  - Khung hỏi `ID=2` có đúng 17 sườn xuống $\rightarrow$ tạo đúng **17 byte `0xFF`**.
  - Khung hỏi `ID=1` có đúng 19 sườn xuống $\rightarrow$ tạo đúng **19 byte `0xFF`**.

### 2.3. Giải pháp khắc phục
- Đo thông mạch bằng đồng hồ vạn năng, phát hiện dây đứt ngầm và thay thế dây nối mới đảm bảo tiếp xúc cơ - điện chắc chắn.

---

## 3. CÁC HẠNG MỤC ĐÃ KIỂM CHỨNG THÀNH CÔNG 100%

1. **Cấu hình COM2 trên PLC Xinje:**
   - Cài đặt: `COM2`, `RTU`, `Station Num = 2`, `8 Data bits`, `Even Parity`, `1 Stop bit` (`8E1`).
   - Tốc độ: Đã kiểm chứng chuẩn ở `9600 bps` (`D503 = 6`), `19200 bps` (`D503 = 7`), `2400 bps` (`D503 = 4`).
   - Đã đọc kiểm tra qua lệnh `CFGCR D500 K8 K2`: `D500=1`, `D501=2`, `D502=0`, `D504=19`.
2. **Kiểm thử giao tiếp thực tế với PLC:**
   - Đọc thành công `D100 = 50`: `02 03 02 00 32 7D 91` (`0x0032 = 50`).
   - Đọc thành công `M100 = 0`: `02 01 01 00 51 CC`.
   - Ghi thành công `D200 = 500`: `02 06 00 C8 01 F4 08 10` $\rightarrow$ PLC xác nhận thanh ghi `D200` cập nhật tức thì.

---

## 4. TÌNH TRẠNG MÃ NGUỒN VÀ HƯỚNG DẪN BƯỚC TIẾP THEO

### 4.1. Cấu trúc mã nguồn trong repository
- `ESP32/platformio.ini`: Đã có sẵn cấu hình `build_src_filter = +<*> -<main_cuong.cpp>` để phục vụ test độc lập Modbus RTU trên `src/main.cpp`.
- `ESP32/src/main.cpp`: Bản firmware test ModbusMaster chuẩn, đã tích hợp:
  - Hàm dọn echo `postTransmissionCallback()` tính toán theo thời gian byte chuẩn.
  - Kéo trở phần cứng qua `gpio_pullup_en((gpio_num_t)MODBUS_RX_PIN)`.
  - Các phím lệnh Serial Monitor: `a` (quét baud), `b9600` / `b19200` (đổi baud nhanh), `s` (Start), `p` (Stop), `r` (Reset), `t500` (Set Target D200).
- `ESP32/src/main_cuong.cpp`: Bản code đã dựng sẵn khung WiFi + PubSubClient + ArduinoJson kết nối MQTT.
- `tools/modbus_test.py`: Script Python kiểm thử đọc/ghi Modbus RTU và nghe lén bus từ máy tính qua cổng `COM8`.

### 4.2. Việc cần làm ở phiên tiếp theo (MQTT Integration)
1. Cắm bộ dây mới đã thông mạch giữa ESP32 và module RS485 (`GPIO 16 -> RXD`, `GPIO 17 -> TXD`, `3V3/5V -> VCC`, `GND -> GND`).
2. Mở `ESP32/src/main_cuong.cpp`, đối soát đồng bộ các thông số Modbus đã chuẩn:
   - `plcSlaveId = 2`
   - `modbusBaud = 9600` (hoặc `19200` tùy cấu hình chọn trên PLC)
   - Cấu hình UART: `Serial2.begin(modbusBaud, SERIAL_8E1, 16, 17);`
3. Chuyển cấu hình `build_src_filter` trong [platformio.ini](file:///D:/Tam/BTL_IOT_UD/BTL-IOT-/ESP32/platformio.ini) sang biên dịch file hoàn chỉnh:
   `build_src_filter = +<*> -<main.cpp>`
4. Nạp code và kiểm tra chu trình hoàn chỉnh: `PLC XD2-16T-E <--(Modbus RTU)--> ESP32 <--(WiFi / MQTT)--> MQTT Broker / Dashboard`.
