#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <NimBLEDevice.h>
#include <math.h>

constexpr const char* deviceName = "Fusor-Meter";
constexpr const char* serviceUuid = "9b3f1001-7f64-4f76-8f6d-8a2f0b6a4c10";
constexpr const char* dataUuid = "9b3f1002-7f64-4f76-8f6d-8a2f0b6a4c10";

constexpr int i2cSdaPin = SDA;
constexpr int i2cSclPin = SCL;
constexpr uint8_t adsAddress = 0x48;

constexpr uint32_t sendIntervalMs = 250;
constexpr uint32_t adsRetryIntervalMs = 3000;
constexpr uint8_t samplesPerReading = 4;

constexpr float adsVoltsPerCount = 0.256f / 32768.0f;

constexpr float hvResistorOhms = 1000000000.0f;
constexpr float voltageSenseOhms = 1000.0f;
constexpr float currentShuntOhms = 3.4f;

constexpr float voltageSign = -1.0f;
constexpr float currentSign = -1.0f;

constexpr bool autoZeroAtBoot = false;
constexpr uint16_t zeroSamples = 64;

constexpr float voltageNormalAlpha = 0.25f;
constexpr float voltageJumpAlpha = 0.85f;
constexpr float voltageJumpThresholdKV = 1.0f;

constexpr float currentNormalAlpha = 0.25f;
constexpr float currentJumpAlpha = 0.85f;
constexpr float currentJumpThresholdMA = 0.15f;

Adafruit_ADS1115 ads;
NimBLECharacteristic* dataChar = nullptr;

bool adsReady = false;
bool clientConnected = false;

unsigned long lastSend = 0;
unsigned long lastAdsRetry = 0;

float voltageZeroVolts = 0.0f;
float currentZeroVolts = 0.0f;

float filteredKV = NAN;
float filteredTotalMA = NAN;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
    clientConnected = true;
    server->updateConnParams(connInfo.getConnHandle(), 24, 48, 0, 180);
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    (void)server;
    (void)connInfo;
    (void)reason;
    clientConnected = false;
    NimBLEDevice::startAdvertising();
  }
};

ServerCallbacks serverCallbacks;

struct DiffReading {
  float volts;
  bool saturated;
};

float adaptiveFilter(float previous, float current, float normalAlpha, float jumpAlpha, float jumpThreshold) {
  if (!isfinite(previous)) {
    return current;
  }

  float difference = fabsf(current - previous);
  float alpha = difference >= jumpThreshold ? jumpAlpha : normalAlpha;
  return previous + alpha * (current - previous);
}

float readAverageRawVoltage() {
  ads.readADC_Differential_0_1();

  long total = 0;
  for (uint8_t i = 0; i < samplesPerReading; i++) {
    total += ads.readADC_Differential_0_1();
  }

  return total / float(samplesPerReading);
}

float readAverageRawCurrent() {
  ads.readADC_Differential_2_3();

  long total = 0;
  for (uint8_t i = 0; i < samplesPerReading; i++) {
    total += ads.readADC_Differential_2_3();
  }

  return total / float(samplesPerReading);
}

DiffReading readVoltageChannel() {
  float rawAverage = readAverageRawVoltage();
  float volts = rawAverage * adsVoltsPerCount * voltageSign - voltageZeroVolts;
  bool saturated = fabsf(rawAverage) > 32000.0f;
  return {volts, saturated};
}

DiffReading readCurrentChannel() {
  float rawAverage = readAverageRawCurrent();
  float volts = rawAverage * adsVoltsPerCount * currentSign - currentZeroVolts;
  bool saturated = fabsf(rawAverage) > 32000.0f;
  return {volts, saturated};
}

void zeroInputs() {
  if (!autoZeroAtBoot || !adsReady) {
    return;
  }

  double voltageTotal = 0.0;
  double currentTotal = 0.0;

  for (uint16_t i = 0; i < zeroSamples; i++) {
    voltageTotal += readAverageRawVoltage() * adsVoltsPerCount * voltageSign;
    currentTotal += readAverageRawCurrent() * adsVoltsPerCount * currentSign;
    delay(5);
  }

  voltageZeroVolts = voltageTotal / double(zeroSamples);
  currentZeroVolts = currentTotal / double(zeroSamples);
}

bool trySetupAds() {
  adsReady = ads.begin(adsAddress);

  if (!adsReady) {
    Serial.printf("ADS1115 not found at 0x%02X\n", adsAddress);
    return false;
  }

  ads.setGain(GAIN_SIXTEEN);
  ads.setDataRate(RATE_ADS1115_128SPS);

  zeroInputs();
  Serial.println("ADS1115 ready");
  return true;
}

void setupAds() {
  Wire.begin(i2cSdaPin, i2cSclPin);
  Wire.setClock(100000);

  lastAdsRetry = millis();
  trySetupAds();
}

void setupBle() {
  NimBLEDevice::init(deviceName);
  NimBLEDevice::setPower(9, NimBLETxPowerType::All);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks, false);

  NimBLEService* service = server->createService(serviceUuid);

  dataChar = service->createCharacteristic(
    dataUuid,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );

  dataChar->setValue("boot");

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->enableScanResponse(true);
  advertising->setMinInterval(32);
  advertising->setMaxInterval(64);
  advertising->addServiceUUID(serviceUuid);
  advertising->setName(deviceName);
  advertising->addTxPower();
  advertising->start();

  Serial.println("BLE advertising");
}

void sendBleMessage(const char* message) {
  dataChar->setValue(message);

  if (clientConnected) {
    dataChar->notify();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Starting Fusor Meter");

  setupAds();
  setupBle();
}

void loop() {
  if (millis() - lastSend < sendIntervalMs) {
    return;
  }

  lastSend = millis();

  char message[96];

  if (!adsReady) {
    if (millis() - lastAdsRetry >= adsRetryIntervalMs) {
      lastAdsRetry = millis();
      if (trySetupAds()) {
        snprintf(message, sizeof(message), "INFO ADS1115 ready");
        Serial.println(message);
        sendBleMessage(message);
        return;
      }
    }

    snprintf(message, sizeof(message), "ERR ADS1115");
    Serial.println(message);
    sendBleMessage(message);
    return;
  }

  DiffReading voltageReading = readVoltageChannel();
  DiffReading currentReading = readCurrentChannel();

  float dividerRatio = (hvResistorOhms + voltageSenseOhms) / voltageSenseOhms;
  float fusorKVRaw = voltageReading.volts * dividerRatio / 1000.0f;
  float totalCurrentMARaw = currentReading.volts / currentShuntOhms * 1000.0f;

  filteredKV = adaptiveFilter(
    filteredKV,
    fusorKVRaw,
    voltageNormalAlpha,
    voltageJumpAlpha,
    voltageJumpThresholdKV
  );

  filteredTotalMA = adaptiveFilter(
    filteredTotalMA,
    totalCurrentMARaw,
    currentNormalAlpha,
    currentJumpAlpha,
    currentJumpThresholdMA
  );

  snprintf(
    message,
    sizeof(message),
    "kv=%.2f,ma=%.3f,sat=%d%d",
    filteredKV,
    filteredTotalMA,
    voltageReading.saturated ? 1 : 0,
    currentReading.saturated ? 1 : 0
  );

  Serial.println(message);
  sendBleMessage(message);
}
