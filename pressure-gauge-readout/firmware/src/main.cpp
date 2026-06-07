#include <Arduino.h>
#include <NimBLEDevice.h>

constexpr const char* deviceName = "PG-XIAO";
constexpr const char* serviceUuid = "9b3f0001-7f64-4f76-8f6d-8a2f0b6a4c10";
constexpr const char* dataUuid = "9b3f0002-7f64-4f76-8f6d-8a2f0b6a4c10";

constexpr uint8_t gaugeRxPin = RX; // XIAO labeled RX/GPIO44, connect to 901P TX
constexpr uint8_t gaugeTxPin = TX; // XIAO labeled TX/GPIO43, connect to 901P RX
constexpr uint32_t gaugeBaud = 9600;
constexpr uint32_t queryIntervalMs = 200;
constexpr uint32_t responseTimeoutMs = 150;
constexpr const char* pressureQuery = "@253PR4?;FF";

NimBLECharacteristic* dataChar = nullptr;
HardwareSerial gauge(1);

int dBm = 14;
unsigned long lastSend = 0;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
    Serial.printf("BLE client connected:\n%s", connInfo.toString().c_str());
    server->updateConnParams(connInfo.getConnHandle(), 24, 48, 0, 180);
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    (void)server;
    (void)connInfo;
    Serial.printf("BLE client disconnected, reason=%d; restarting advertising\n", reason);
    NimBLEDevice::startAdvertising();
  }
};

ServerCallbacks serverCallbacks;

String parseNumericPart(const String& response) {
  int ackIndex = response.indexOf("ACK");
  if (ackIndex < 0) {
    return "";
  }

  int startIndex = ackIndex + 3;
  int endIndex = response.indexOf(";FF", startIndex);
  if (endIndex <= startIndex) {
    return "";
  }

  String numericPart = response.substring(startIndex, endIndex);
  numericPart.trim();
  return numericPart;
}

String readGaugeResponse(uint32_t timeoutMs) {
  String response;
  unsigned long start = millis();

  while (millis() - start < timeoutMs) {
    while (gauge.available()) {
      char c = static_cast<char>(gauge.read());
      response += c;

      if (response.indexOf(";FF") >= 0) {
        return response;
      }
    }

    delay(1);
  }

  return response;
}

void clearGaugeInput() {
  while (gauge.available()) {
    gauge.read();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  gauge.begin(gaugeBaud, SERIAL_8N1, gaugeRxPin, gaugeTxPin);
  gauge.setTimeout(responseTimeoutMs);

  Serial.println("Starting BLE Pressure Monitor");
  Serial.printf("901P gauge UART started at %lu baud, RX pin=GPIO%u, TX pin=GPIO%u\n",
                gaugeBaud,
                gaugeRxPin,
                gaugeTxPin);

  NimBLEDevice::init(deviceName);
  bool powerSet = NimBLEDevice::setPower(dBm, NimBLETxPowerType::All);
  Serial.println("BLE initialized");
  Serial.printf("BLE TX power set: %s, advertise power=%d dBm\n",
                powerSet ? "yes" : "no",
                NimBLEDevice::getPower(NimBLETxPowerType::Advertise));

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks, false);
  NimBLEService* service = server->createService(serviceUuid);

  dataChar = service->createCharacteristic(
    dataUuid,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  Serial.println("BLE service and characteristic created");
  dataChar->setValue("boot");

  server->start();
  Serial.println("BLE server started");
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->enableScanResponse(true);
  advertising->setMinInterval(32);
  advertising->setMaxInterval(64);

  bool serviceAdvertised = advertising->addServiceUUID(serviceUuid);
  bool nameAdvertised = advertising->setName(deviceName);
  bool txPowerAdvertised = advertising->addTxPower();
  bool advertisingStarted = advertising->start();

  Serial.printf("BLE service advertised: %s\n", serviceAdvertised ? "yes" : "no");
  Serial.printf("BLE name advertised: %s\n", nameAdvertised ? "yes" : "no");
  Serial.printf("BLE TX power advertised: %s\n", txPowerAdvertised ? "yes" : "no");
  Serial.printf("BLE advertising started: %s\n", advertisingStarted ? "yes" : "no");
}

void loop() {
  if (millis() - lastSend >= queryIntervalMs) {
    lastSend = millis();

    clearGaugeInput();
    gauge.print(pressureQuery);

    String response = readGaugeResponse(responseTimeoutMs);
    String numericPart = parseNumericPart(response);
    String message;

    if (numericPart.length() > 0) {
      float value = numericPart.toFloat();
      message = String(value, 7) + " Torr";
    } else if (response.length() > 0) {
      message = "ERR parse";
      Serial.print("901P parse error: ");
      Serial.println(response);
    } else {
      message = "ERR timeout";
      Serial.println("901P timeout: no response");
    }

    dataChar->setValue(message.c_str());
    dataChar->notify();

    Serial.println(message);
  }
}
