# SYSTEM CONTEXT V2: IIoT GATEWAY FOR PRODUCT COUNTING (WITH BATCH & INTERLOCK)

## 1. SYSTEM ARCHITECTURE & WIRING
- **PLC:** Xinje XD2-16T-E (Slave ID 1). Sensor X0, Conveyor Y0, Done_Light Y1.
- **Gateway:** ESP32 WROOM + MAX485 (Auto-direction). UART2 (RX:16, TX:17).
- **Protocol:** Modbus RTU (ESP32 <-> PLC) & MQTT (ESP32 <-> Cloud).

## 2. DATA MAPPING (MODBUS TO MQTT)
### A. Monitor (Upstream: PLC -> ESP32 -> MQTT)
- **Modbus Read (Polling 1000ms):** 
  - FC 03: `D100` (Current Count).
  - FC 01: `Y0`-> M100 (Conveyor Status).
- **MQTT Publish (`factory/machine1/data`):**
  - Payload: `{"count": 150, "status": "running", "target": 500}`

### B. Control (Downstream: App -> MQTT -> ESP32 -> PLC)
- **MQTT Subscribe (`factory/machine1/command`):**
  - App sends: `{"cmd": "start"}`, `{"cmd": "stop"}`, `{"cmd": "reset"}`, `{"set_target": 1000}`
- **Modbus Write (Triggered by MQTT):**
  - FC 06: Write to `D200` (Target Count).
  - FC 05: Write `M0` ON (Start), `M1` ON (Stop), `M10` ON (Reset Batch).

## 3. PLC LADDER LOGIC (Strict Interlock)
- **Run/Stop Logic:** 
  - If `M0` is ON -> SET `Y0` (Conveyor runs).
  - If `M1` is ON -> RESET `Y0` (Conveyor stops).
- **Counting Interlock:** 
  - (Rising Edge `X0`) AND (`Y0` is ON) -> `INC D100`.
- **Target Reach Logic:** 
  - If `D100` >= `D200` -> RESET `Y0` (Auto-stop), SET `Y1` (Done light ON).
- **Reset Logic:**
  - If `M10` is ON -> `RST D100`, `RST Y1` (Clear count and light).