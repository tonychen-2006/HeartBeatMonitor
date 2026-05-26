#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

typedef struct {
    int ecgValue;
} ECGPacket;

ECGPacket receivedPacket;

// LED pins on ESP32-WROOM-32
int leds[] = {13, 12, 14, 27, 26};
const int numLeds = 5;

void showIntensity(int ecg) {
    // Your AD8232 values were roughly around 1800–2100
    int level = map(ecg, 1800, 2100, 0, numLeds);
    level = constrain(level, 0, numLeds);

    for (int i = 0; i < numLeds; i++) {
        digitalWrite(leds[i], i < level ? HIGH : LOW);
    }
}

// For your PlatformIO ESP32 Arduino core version, use this callback format
void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    memcpy(&receivedPacket, incomingData, sizeof(receivedPacket));

    Serial.print("Received ECG: ");
    Serial.println(receivedPacket.ecgValue);

    showIntensity(receivedPacket.ecgValue);
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    for (int i = 0; i < numLeds; i++) {
        pinMode(leds[i], OUTPUT);
        digitalWrite(leds[i], LOW);
    }

    WiFi.mode(WIFI_STA);

    Serial.println();
    Serial.print("Receiver MAC Address: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }

    esp_now_register_recv_cb(onDataRecv);

    Serial.println("Receiver ready. Waiting for ECG data...");
}

void loop() {
    // Nothing needed here.
    // Data is handled whenever a packet arrives.
}