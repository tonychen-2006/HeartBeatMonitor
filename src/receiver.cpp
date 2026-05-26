#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

#define LED_PIN D0
#define LED_ON_TIME_MS 80

typedef struct {
    bool pulse;
} PulseMessage;

volatile bool pulseReceived = false;

bool ledActive = false;
unsigned long ledStartTime = 0;

void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
    if (len == sizeof(PulseMessage)) {
        PulseMessage incomingMessage;
        memcpy(&incomingMessage, data, sizeof(incomingMessage));

        if (incomingMessage.pulse) {
            pulseReceived = true;
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    WiFi.mode(WIFI_STA);

    Serial.print("XIAO receiver MAC: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }

    esp_now_register_recv_cb(onDataRecv);

    Serial.println("XIAO receiver ready");
}

void loop() {
    if (pulseReceived) {
        pulseReceived = false;

        digitalWrite(LED_PIN, HIGH);
        ledStartTime = millis();
        ledActive = true;

        Serial.println("Pulse received - LED ON");
    }

    if (ledActive && millis() - ledStartTime >= LED_ON_TIME_MS) {
        digitalWrite(LED_PIN, LOW);
        ledActive = false;
    }
}