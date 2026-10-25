#include <Arduino.h>

// Hầu hết ESP32 WROOM đều có LED gắn sẵn ở GPIO 2
constexpr uint8_t ONBOARD_LED = 2;

// put function declarations here:
int myFunction(int, int);

void setup() {
  // put your setup code here, to run once:
  int result = myFunction(2, 3);

    // Cấu hình chân GPIO 2 làm đầu ra (Output)
  pinMode(ONBOARD_LED, OUTPUT);
  // Mở cổng Serial USB với tốc độ 115200 baud
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=================================");
  Serial.println("  HELLO WORLD TU ESP32 WROOM!   ");
  Serial.println("=================================");
}

void loop() {
  // put your main code here, to run repeatedly:
    // Bật đèn LED
  digitalWrite(ONBOARD_LED, HIGH);
  Serial.println("-> LED: ON (Bat)");
  delay(200); // Chờ 1 giây
  // Tắt đèn LED
  digitalWrite(ONBOARD_LED, LOW);
  Serial.println("-> LED: OFF (Tat)");
  delay(200); // Chờ 1 giây
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}