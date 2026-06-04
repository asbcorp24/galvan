#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_PN532.h>

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_ADDR 0x3C

#define I2C_SDA 21
#define I2C_SCL 22

#define PN532_X_IRQ   25
#define PN532_X_RESET 27

#define PN532_Z_RX    36
#define PN532_Z_TX    33
#define PN532_Z_RESET 12

Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
Adafruit_PN532 nfcX(PN532_X_IRQ, PN532_X_RESET, &Wire);
HardwareSerial pn532ZSerial(2);
Adafruit_PN532 nfcZ(PN532_Z_RESET, &pn532ZSerial);

static bool oledReady = false;
static bool nfcXReady = false;
static bool nfcZReady = false;
static bool oledSeenOnBus = false;
static bool nfcXSeenOnBus = false;

static bool i2cDevicePresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static void scanI2C() {
  Serial.println("[I2C] scan start");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.printf("[I2C] found 0x%02X\n", addr);
      found++;
    }
  }
  Serial.printf("[I2C] scan done, found=%d\n", found);
}

static void drawStatus(const char *line1,
                       const char *line2 = nullptr,
                       const char *line3 = nullptr) {
  if (!oledReady) return;

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println(line1 ? line1 : "");
  if (line2) oled.println(line2);
  if (line3) oled.println(line3);
  oled.display();
}

static void initOled() {
  Serial.println("[OLED] init start");
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  delay(50);

  oledSeenOnBus = i2cDevicePresent(OLED_ADDR);
  if (!oledSeenOnBus) {
    Serial.println("[OLED] not found at 0x3C");
    return;
  }

  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[OLED] begin failed");
    return;
  }

  oledReady = true;
  Serial.println("[OLED] init done");
  drawStatus("OLED OK", "Waiting init...");
}

static void initPn532X() {
  Serial.println("[PN532-X] init start");
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  delay(100);

  nfcXSeenOnBus = i2cDevicePresent(0x24);
  Serial.printf("[PN532-X] bus check 0x24 -> %s\n", nfcXSeenOnBus ? "ACK" : "no ACK");
  if (!nfcXSeenOnBus) {
    Serial.println("[PN532-X] skip begin, device not seen on I2C bus");
    return;
  }

  // Leave the module RSTO pin physically disconnected.
  nfcX.begin();
  Serial.println("[PN532-X] begin returned");

  uint32_t version = nfcX.getFirmwareVersion();
  Serial.printf("[PN532-X] version=0x%08lX\n", (unsigned long)version);
  if (!version) {
    Serial.println("[PN532-X] firmware read failed");
    return;
  }

  nfcX.setPassiveActivationRetries(0x05);
  nfcXReady = true;
  Serial.println("[PN532-X] init done");
}

static void initPn532Z() {
  Serial.println("[PN532-Z] init start");
  pinMode(PN532_Z_RESET, OUTPUT);
  digitalWrite(PN532_Z_RESET, LOW);
  delay(10);
  digitalWrite(PN532_Z_RESET, HIGH);
  delay(50);

  pn532ZSerial.begin(115200, SERIAL_8N1, PN532_Z_RX, PN532_Z_TX);
  if (!nfcZ.begin()) {
    Serial.println("[PN532-Z] begin failed");
    return;
  }

  uint32_t version = nfcZ.getFirmwareVersion();
  Serial.printf("[PN532-Z] version=0x%08lX\n", (unsigned long)version);
  if (!version) {
    Serial.println("[PN532-Z] firmware read failed");
    return;
  }

  nfcZ.setPassiveActivationRetries(0x05);
  nfcZ.SAMConfig();
  nfcZReady = true;
  Serial.println("[PN532-Z] init done");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("===== hwtest init =====");
  Serial.printf("[PINMAP] I2C SDA=%d SCL=%d\n", I2C_SDA, I2C_SCL);
  Serial.printf("[PINMAP] X IRQ=%d RST=%d\n", PN532_X_IRQ, PN532_X_RESET);
  Serial.printf("[PINMAP] Z RX=%d TX=%d RST=%d\n", PN532_Z_RX, PN532_Z_TX, PN532_Z_RESET);

  initOled();
  scanI2C();
  drawStatus(oledReady ? "OLED OK" : "OLED FAIL", "I2C scan done");

  initPn532X();
  drawStatus("X init done", nfcXReady ? "X READY" : "X FAIL");

  initPn532Z();
  drawStatus("Init complete",
             nfcXReady ? "X READY" : "X FAIL",
             nfcZReady ? "Z READY" : "Z FAIL");

  Serial.printf("[READY] oled=%d x=%d z=%d\n",
                (int)oledReady, (int)nfcXReady, (int)nfcZReady);
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last < 1000) {
    delay(10);
    return;
  }
  last = millis();

  Serial.printf("[HEARTBEAT] oled=%d x=%d z=%d uptime=%lu\n",
                (int)oledReady, (int)nfcXReady, (int)nfcZReady,
                (unsigned long)millis());
}
