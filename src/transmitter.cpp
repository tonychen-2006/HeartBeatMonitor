#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h> // Swapped to ST7789 Library

uint8_t xiaoAddress[] = {0x14, 0xC1, 0x9F, 0xC6, 0x8F, 0x40}; // ESP receiver MAC address

typedef struct {
  bool pulse;
} PulseMessage;

PulseMessage pulseMessage;

// Pins
#define PULSE_PIN 34
#define LED_PIN 27

// ST7789 TFT SPI pins
#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4
#define TFT_SCLK 18
#define TFT_MOSI 23

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

const int RAW_TOO_HIGH = 4050;
const int RAW_TOO_LOW  = 50;

float filtered = 2000;
float baseline = 2000;

float peakTracker = 0;
bool ledActive = false;
unsigned long ledOnTime = 0;
unsigned long lastPulseTime = 0;
const unsigned long MIN_PULSE_INTERVAL_MS = 400; 

int screenW;
int screenH;
int centerY;

int graphX = 0;
int lastY = 0;

float betaPhase1 = 0;
float betaPhase2 = 0;

unsigned long lastGraphUpdate = 0;
const unsigned long GRAPH_INTERVAL_MS = 16; // Faster 60Hz update for ST7789

bool sensorConnected = false;
bool lastSensorState = false; 

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // No serial spam
}

void setupEspNow() {
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, xiaoAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (!esp_now_is_peer_exist(xiaoAddress)) {
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("Failed to add ESP-NOW peer");
    }
  }
}

void sendPulse(bool state) {
  pulseMessage.pulse = state;
  esp_now_send(xiaoAddress, (uint8_t *)&pulseMessage, sizeof(pulseMessage));
}

void drawHeader(bool forceRedraw = false) {
  if (sensorConnected == lastSensorState && !forceRedraw) {
    return; 
  }
  
  lastSensorState = sensorConnected;

  tft.fillRect(0, 0, screenW, 30, ST77XX_BLACK);
  tft.setTextSize(2); // Increased text size slightly for higher res ST7789
  tft.setCursor(8, 6);

  if (sensorConnected) {
    tft.setTextColor(ST77XX_GREEN);
    tft.print("EEG: ACTIVE");
  } else {
    tft.setTextColor(ST77XX_RED);
    tft.print("EEG: NO SIGNAL");
  }
}

void setupDisplay() {

  tft.init(240, 240); 
  
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  screenW = tft.width();
  screenH = tft.height();
  centerY = screenH / 2;
  lastY = centerY;

  drawHeader(true); 
  tft.drawFastHLine(0, centerY, screenW, ST77XX_BLUE);
}

bool readSensor(float &sensorWave) {
  int raw = analogRead(PULSE_PIN);

  if (raw >= RAW_TOO_HIGH || raw <= RAW_TOO_LOW) {
    sensorConnected = false;
    sensorWave = 0;
    digitalWrite(LED_PIN, LOW);
    ledActive = false;
    return false;
  }

  sensorConnected = true;

  filtered = filtered * 0.80 + raw * 0.20;
  baseline = baseline * 0.996 + filtered * 0.004;
  sensorWave = filtered - baseline;

  unsigned long now = millis();
  bool pulseTriggered = false;

  peakTracker = peakTracker * 0.995; 
  if (sensorWave > peakTracker) {
    peakTracker = sensorWave; 
  }

  float dynamicThreshold = max(40.0f, peakTracker * 0.35f);

  if (!ledActive && (sensorWave > dynamicThreshold)) {
    if (now - lastPulseTime > MIN_PULSE_INTERVAL_MS) {
      digitalWrite(LED_PIN, HIGH);
      ledOnTime = now;
      lastPulseTime = now;
      ledActive = true;
      pulseTriggered = true;
      sendPulse(true);
    }
  }

  if (ledActive && (sensorWave < dynamicThreshold * 0.5)) {
    ledActive = false;
  }

  if (digitalRead(LED_PIN) == HIGH && (now - ledOnTime > 90)) {
    digitalWrite(LED_PIN, LOW);
  }

  return pulseTriggered;
}

void drawWave(float sensorWave, bool pulseTriggered) {
  // Clear vertical strip ahead
  tft.drawFastVLine(graphX, 32, screenH - 32, ST77XX_BLACK);
  tft.drawPixel(graphX, centerY, ST77XX_BLUE);

  betaPhase1 += 0.55;
  betaPhase2 += 0.95;

  if (betaPhase1 > TWO_PI) betaPhase1 -= TWO_PI;
  if (betaPhase2 > TWO_PI) betaPhase2 -= TWO_PI;

  float beta = sin(betaPhase1) * 0.60 + sin(betaPhase2) * 0.40;
  int amp;

  if (sensorConnected) {
    float boost = constrain(abs(sensorWave) / 100.0, 0.0, 2.5);
    amp = 30 + boost * 25; 
  } else {
    amp = 12;
  }

  int y = centerY + beta * amp;
  y = constrain(y, 35, screenH - 6);

  uint16_t color;
  if (!sensorConnected) {
    color = ST77XX_YELLOW;
  } else if (digitalRead(LED_PIN) == HIGH) {
    color = ST77XX_RED; // Turns line Red while LED physically fires
  } else {
    color = ST77XX_GREEN;
  }

  if (graphX > 0) {
    tft.drawLine(graphX - 1, lastY, graphX, y, color);
  }

  lastY = y;
  graphX++;

  if (graphX >= screenW) {
    graphX = 0;
    lastY = centerY;
    tft.fillRect(0, 32, screenW, screenH - 32, ST77XX_BLACK);
    tft.drawFastHLine(0, centerY, screenW, ST77XX_BLUE);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  SPI.begin(); 
  setupDisplay();
  setupEspNow();

  long sum = 0;
  int validSamples = 0;

  for (int i = 0; i < 100; i++) {
    int raw = analogRead(PULSE_PIN);

    if (raw > RAW_TOO_LOW && raw < RAW_TOO_HIGH) {
      sum += raw;
      validSamples++;
    }
    delay(5);
  }

  if (validSamples > 0) {
    baseline = sum / validSamples;
    filtered = baseline;
  } else {
    baseline = 2000;
    filtered = 2000;
  }

  Serial.println("ST7789 System Initialized with Baseline Calibration.");
}

static bool hitPulse = false;

void loop() {
  float sensorWave = 0;

  bool currentPulse = readSensor(sensorWave);
  if (currentPulse) {
    hitPulse = true; 
  }

  unsigned long now = millis();
  if (now - lastGraphUpdate >= GRAPH_INTERVAL_MS) {
    lastGraphUpdate = now;
    drawWave(sensorWave, hitPulse);
    hitPulse = false; // reset
  }

  drawHeader();

  static unsigned long lastPrint = 0;
  if (now - lastPrint > 700) {
    lastPrint = now;
    int raw = analogRead(PULSE_PIN);
    Serial.print("RawADC: "); Serial.print(raw);
    Serial.print(" | WaveOffset: "); Serial.print(sensorWave);
    Serial.print(" | Connected: "); Serial.println(sensorConnected ? "YES" : "NO");
  }
}