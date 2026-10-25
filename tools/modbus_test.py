"""Test Modbus RTU PLC Xinje bang USB-RS485 tren may tinh. Sua cau hinh ngay duoi day, roi bam Run.

MODE = "TEST"   : PC hoi PLC (doc D100, M100, ghi D200 = 500)
MODE = "LISTEN" : PC chi NGHE bus A/B (de xem ESP32 + module thuc su phat ra gi toi PLC)
Can: pip install pyserial
"""
import time

import serial

# ======================= CAU HINH =======================
PORT = "COM8"
BAUD = 2400       # Doi cung toc do voi PLC (2400 bps)
PARITY = "E"      # N / E / O
SLAVE_ID = 2
MODE = "TEST"   # "TEST" hoac "LISTEN"
LISTEN_SECONDS = 20
# ========================================================


def crc16(data: bytes) -> bytes:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return bytes([crc & 0xFF, crc >> 8])


def txrx(ser, pdu: bytes, wait=0.3) -> bytes:
    frame = pdu + crc16(pdu)
    ser.reset_input_buffer()
    ser.write(frame)
    ser.flush()
    time.sleep(wait)
    rx = ser.read(ser.in_waiting or 1)
    print("  TX:", frame.hex(" ").upper())
    print("  RX (%d bytes):" % len(rx), rx.hex(" ").upper())
    return rx


def run_test(ser):
    print("[1] Doc D100 (FC03, addr 100):")
    txrx(ser, bytes([SLAVE_ID, 0x03, 0x00, 100, 0x00, 0x01]))
    print("[2] Doc M100 (FC01, addr 100):")
    txrx(ser, bytes([SLAVE_ID, 0x01, 0x00, 100, 0x00, 0x01]))
    print("[3] Ghi D200 = 500 (FC06, addr 200):")
    txrx(ser, bytes([SLAVE_ID, 0x06, 0x00, 200, 0x01, 0xF4]))


def run_listen(ser):
    print("Dang NGHE bus %d giay (bat ESP32 phat). Moi khung = 1 dong:" % LISTEN_SECONDS)
    end = time.time() + LISTEN_SECONDS
    buf = b""
    last = time.time()
    while time.time() < end:
        data = ser.read(64)
        now = time.time()
        if data:
            buf += data
            last = now
        elif buf and now - last > 0.02:
            ok = len(buf) >= 4 and crc16(buf[:-2]) == buf[-2:]
            print("  t=%.2fs [%d bytes] %s  -> CRC %s" % (now - end + LISTEN_SECONDS, len(buf), buf.hex(" ").upper(), "DUNG" if ok else "SAI"))
            buf = b""


def main():
    ser = serial.Serial(PORT, BAUD, bytesize=8, parity=PARITY, stopbits=1, timeout=0.05)
    try:
        run_test(ser) if MODE == "TEST" else run_listen(ser)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
