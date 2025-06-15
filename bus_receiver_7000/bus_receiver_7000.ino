#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

#define CE_PIN   9
#define CSN_PIN 10
#define LED_PIN 8

RF24 radio(CE_PIN, CSN_PIN);
const byte address[6] = "00001";

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  radio.begin();
  radio.setChannel(0x4C);              // 송신기와 일치
  radio.setDataRate(RF24_1MBPS);       // 송신기와 일치
  radio.setPALevel(RF24_PA_MIN);
  radio.openReadingPipe(1, address);
  radio.startListening();
  digitalWrite(LED_PIN, LOW);
  Serial.println("Ready to receive...");
}

void loop() {
  if (radio.available()) {
    char buffer[33] = {0};  // 32바이트 + null
    radio.read(&buffer, 32);
    String received = String(buffer);
    received.trim();

    Serial.print("수신: ");
    Serial.println(received);

    if (received == "7000") {
      digitalWrite(LED_PIN, HIGH);  // LED ON
      Serial.println("LED ON!");
    } else {
         // LED OFF
    }
  }
}
