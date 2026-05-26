#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// XIAO ESP32S3 receiver MAC address
uint8_t xiaoAddress[] = {0x14, 0xC1, 0x9F, 0xC6, 0x8F, 0x40};

#define PULSE_PIN 34

typedef struct {
    bool pulse;
} PulseMessage;

PulseMessage pulseMessage;

// More tolerant timing
const unsigned long MIN_PULSE_INTERVAL_MS = 300;  // allows up to 200 BPM
const unsigned long PEAK_TIMEOUT_MS = 120;

// Signal filtering
float filteredValue = 0;
float baseline = 0;

// Detection settings
const float FILTER_ALPHA = 0.25;     // higher = more responsive
const float BASELINE_ALPHA = 0.01;   // slow baseline tracking
const int THRESHOLD_OFFSET = 180;    // pulse must rise this much above baseline

bool inPulse = false;
unsigned long lastPulseTime = 0;
unsigned long pulseStartTime = 0;

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("Send status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

void sendPulse() {
    pulseMessage.pulse = true;

    esp_err_t result = esp_now_send(
        xiaoAddress,
        (uint8_t *)&pulseMessage,
        sizeof(pulseMessage)
    );

    if (result == ESP_OK) {
        Serial.println("Pulse sent");
    } else {
        Serial.print("Send error: ");
        Serial.println(result);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(PULSE_PIN, INPUT);

    WiFi.mode(WIFI_STA);

    Serial.print("ESP32 transmitter MAC: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }

    esp_now_register_send_cb(onDataSent);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, xiaoAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add XIAO peer");
        return;
    }

    // Initialize baseline
    int initial = analogRead(PULSE_PIN);
    filteredValue = initial;
    baseline = initial;

    Serial.println("ESP32 transmitter ready");
}

void loop() {
    int rawValue = analogRead(PULSE_PIN);

    // More responsive smoothing
    filteredValue = (1.0 - FILTER_ALPHA) * filteredValue + FILTER_ALPHA * rawValue;

    // Slowly follow the resting signal level
    baseline = (1.0 - BASELINE_ALPHA) * baseline + BASELINE_ALPHA * filteredValue;

    float dynamicThreshold = baseline + THRESHOLD_OFFSET;
    unsigned long now = millis();

    // Detect rising pulse above adaptive threshold
    if (!inPulse && filteredValue > dynamicThreshold) {
        if (now - lastPulseTime >= MIN_PULSE_INTERVAL_MS) {
            sendPulse();
            lastPulseTime = now;

            Serial.print("Pulse detected | raw=");
            Serial.print(rawValue);
            Serial.print(" filtered=");
            Serial.print(filteredValue);
            Serial.print(" baseline=");
            Serial.print(baseline);
            Serial.print(" threshold=");
            Serial.println(dynamicThreshold);
        }

        inPulse = true;
        pulseStartTime = now;
    }

    // Re-arm after signal drops near baseline OR after timeout
    if (inPulse) {
        if (filteredValue < baseline + 60 || now - pulseStartTime > PEAK_TIMEOUT_MS) {
            inPulse = false;
        }
    }

    delay(5);
}