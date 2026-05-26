#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

#define ECG_PIN A0

uint8_t receiverMac[] = {0x20, 0x43, 0xA8, 0x65, 0xFF, 0xC0};

typedef struct {
    int ecgValue;
} ECGPacket;

ECGPacket packet;

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("Send status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    WiFi.mode(WIFI_STA);

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }

    esp_now_register_send_cb(onDataSent);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverMac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }

    Serial.println("XIAO transmitter ready");
}

void loop() {
    packet.ecgValue = analogRead(ECG_PIN);

    esp_err_t result = esp_now_send(
        receiverMac,
        reinterpret_cast<uint8_t *>(&packet),
        sizeof(packet)
    );

    Serial.print("Sent ECG: ");
    Serial.print(packet.ecgValue);
    Serial.print(" | Result: ");
    Serial.println(result == ESP_OK ? "OK" : "ERROR");

    delay(20);
}