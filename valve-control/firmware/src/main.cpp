#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>

constexpr int stepPin = D0;
constexpr int dirPin = D1;
constexpr int enablePin = D2;

constexpr bool enableActiveLow = false;
constexpr bool invertDirection = true;
constexpr int stepPulseUs = 700;
constexpr int stepGapUs = 700;

constexpr long closedLimitSteps = 0;
constexpr int defaultJogSteps = 40;

constexpr const char* deviceName = "Fusor-Valve";
constexpr const char* serviceUuid = "9b3f2001-7f64-4f76-8f6d-8a2f0b6a4c10";
constexpr const char* statusUuid = "9b3f2002-7f64-4f76-8f6d-8a2f0b6a4c10";
constexpr const char* commandUuid = "9b3f2003-7f64-4f76-8f6d-8a2f0b6a4c10";
constexpr uint32_t heartbeatIntervalMs = 1000;

enum ValveMode : uint8_t {
  MODE_CALIBRATE = 0,
  MODE_RUN = 1,
};

Preferences prefs;

long positionSteps = 0;
long openLimitSteps = 0;
ValveMode currentMode = MODE_CALIBRATE;
int jogSteps = defaultJogSteps;

NimBLECharacteristic* statusChar = nullptr;
bool clientConnected = false;
unsigned long lastStatusSentMs = 0;

const char* modeName(ValveMode m) {
  return m == MODE_RUN ? "RUN" : "CALIBRATE";
}

void setDriverEnabled(bool enabled) {
  digitalWrite(enablePin, enableActiveLow ? !enabled : enabled);
}

void savePosition() {
  prefs.putLong("pos", positionSteps);
}

void saveOpenLimit() {
  prefs.putLong("open", openLimitSteps);
}

void saveMode() {
  prefs.putUChar("mode", static_cast<uint8_t>(currentMode));
}

void printStatus() {
  Serial.printf("mode=%s pos=%ld open=%ld jog=%d\n",
                modeName(currentMode), positionSteps, openLimitSteps, jogSteps);
}

void sendStatus() {
  if (!statusChar) {
    return;
  }
  char buffer[96];
  snprintf(buffer, sizeof(buffer),
           "pos=%ld,open=%ld,mode=%s",
           positionSteps, openLimitSteps, modeName(currentMode));
  statusChar->setValue(buffer);
  if (clientConnected) {
    statusChar->notify();
  }
  lastStatusSentMs = millis();
}

long clampTarget(long target) {
  if (currentMode == MODE_CALIBRATE) {
    return target;
  }
  if (target < closedLimitSteps) {
    return closedLimitSteps;
  }
  if (openLimitSteps > 0 && target > openLimitSteps) {
    return openLimitSteps;
  }
  return target;
}

void moveTo(long target) {
  target = clampTarget(target);
  long delta = target - positionSteps;

  if (delta == 0) {
    printStatus();
    sendStatus();
    return;
  }

  bool openDirection = delta > 0;
  long steps = labs(delta);
  bool dirHigh = openDirection != invertDirection;

  setDriverEnabled(true);
  digitalWrite(dirPin, dirHigh ? HIGH : LOW);
  delayMicroseconds(20);

  for (long i = 0; i < steps; i++) {
    digitalWrite(stepPin, LOW);
    delayMicroseconds(stepPulseUs);
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(stepGapUs);
  }
  setDriverEnabled(false);

  positionSteps = target;
  savePosition();
  printStatus();
  sendStatus();
}

void jog(bool openDirection) {
  long target = positionSteps + (openDirection ? jogSteps : -jogSteps);
  moveTo(target);
}

void setMode(ValveMode mode) {
  currentMode = mode;
  saveMode();
  if (mode == MODE_RUN) {
    long clamped = clampTarget(positionSteps);
    if (clamped != positionSteps) {
      positionSteps = clamped;
      savePosition();
    }
  }
  Serial.printf("mode -> %s\n", modeName(currentMode));
}

void printBanner() {
  Serial.println();
  Serial.println("==========================================");
  Serial.println("           FUSOR VALVE");
  Serial.println("==========================================");
  Serial.printf("  mode = %s\n", modeName(currentMode));
  Serial.printf("  pos  = %ld\n", positionSteps);
  Serial.printf("  open = %ld\n", openLimitSteps);
  Serial.printf("  BLE  = %s\n", deviceName);
  Serial.println("==========================================");
  if (openLimitSteps <= 0) {
    Serial.println("  No open limit yet. In CALIBRATE mode, jog to the");
    Serial.println("  mechanical open position and press 'm' (or use");
    Serial.println("  ':SETOPEN <n>' if you know the value).");
  }
  if (positionSteps == 0 && openLimitSteps > 0) {
    Serial.println("  NOTE: pos = 0. If the valve is NOT physically at the");
    Serial.println("  closed reference, correct it with ':SETPOS <n>'");
    Serial.println("  before switching to RUN mode.");
  }
  Serial.println();
}

void printHelp() {
  Serial.println("Single-char commands:");
  Serial.println("  a / left arrow   = jog closed");
  Serial.println("  d / right arrow  = jog open");
  Serial.println("  c                = go to closed (0)");
  Serial.println("  o                = go to open limit");
  Serial.println("  p                = print status");
  Serial.println("  z                = set current position as zero");
  Serial.println("  m                = mark current position as open limit");
  Serial.println("  [ / ]            = smaller / bigger jog");
  Serial.println("  ?                = help");
  Serial.println();
  Serial.println("Text commands (type ':' then command + Enter):");
  Serial.println("  :MODE CALIBRATE | :MODE RUN");
  Serial.println("  :SETOPEN <n>     restore open limit (calibration value)");
  Serial.println("  :SETPOS <n>      assert current position");
  Serial.println("  :MOVETO <n>      move to absolute step");
  Serial.println("  :MOVE <d>        move by delta steps");
  Serial.println("  :CLOSE | :OPEN | :STATUS");
  Serial.println();
}

void handleChar(char c) {
  if (c == 'a' || c == 'A') {
    jog(false);
  } else if (c == 'd' || c == 'D') {
    jog(true);
  } else if (c == 'c' || c == 'C') {
    moveTo(closedLimitSteps);
  } else if (c == 'o' || c == 'O') {
    if (openLimitSteps > 0) {
      moveTo(openLimitSteps);
    } else {
      Serial.println("open limit not set");
    }
  } else if (c == 'p' || c == 'P') {
    printStatus();
    sendStatus();
  } else if (c == 'z' || c == 'Z') {
    positionSteps = 0;
    savePosition();
    Serial.println("zeroed");
    sendStatus();
  } else if (c == 'm' || c == 'M') {
    openLimitSteps = positionSteps;
    saveOpenLimit();
    Serial.println("open limit marked");
    sendStatus();
  } else if (c == '[') {
    jogSteps = max(1, jogSteps / 2);
    printStatus();
  } else if (c == ']') {
    jogSteps = min(3200, jogSteps * 2);
    printStatus();
  } else if (c == '?') {
    printHelp();
  }
}

bool startsWith(const String& s, const char* prefix) {
  return s.startsWith(prefix);
}

void executeRemoteCommand(const String& raw) {
  String cmd = raw;
  cmd.trim();
  if (cmd.length() == 0) {
    return;
  }

  Serial.print("cmd: ");
  Serial.println(cmd);

  String upper = cmd;
  upper.toUpperCase();

  if (startsWith(upper, "MOVETO ")) {
    moveTo(cmd.substring(7).toInt());
  } else if (startsWith(upper, "MOVE ")) {
    moveTo(positionSteps + cmd.substring(5).toInt());
  } else if (upper == "CLOSE") {
    moveTo(closedLimitSteps);
  } else if (upper == "OPEN") {
    if (openLimitSteps > 0) {
      moveTo(openLimitSteps);
    } else {
      Serial.println("open limit not set");
      sendStatus();
    }
  } else if (startsWith(upper, "SETOPEN ")) {
    long value = cmd.substring(8).toInt();
    if (value <= 0) {
      Serial.println("SETOPEN requires a positive value");
      sendStatus();
      return;
    }
    openLimitSteps = value;
    saveOpenLimit();
    positionSteps = clampTarget(positionSteps);
    savePosition();
    sendStatus();
  } else if (startsWith(upper, "SETPOS ")) {
    positionSteps = clampTarget(cmd.substring(7).toInt());
    savePosition();
    sendStatus();
  } else if (upper == "MODE CALIBRATE") {
    setMode(MODE_CALIBRATE);
    sendStatus();
  } else if (upper == "MODE RUN") {
    setMode(MODE_RUN);
    sendStatus();
  } else if (upper == "STATUS?" || upper == "STATUS") {
    printStatus();
    sendStatus();
  } else {
    Serial.print("cmd unknown: ");
    Serial.println(cmd);
    sendStatus();
  }
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
    clientConnected = true;
    server->updateConnParams(connInfo.getConnHandle(), 24, 48, 0, 180);
    Serial.println("BLE client connected");
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    (void)server;
    (void)connInfo;
    (void)reason;
    clientConnected = false;
    Serial.printf("BLE disconnected reason=%d\n", reason);
    NimBLEDevice::startAdvertising();
  }
};

class CommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
    (void)connInfo;
    std::string value = characteristic->getValue();
    if (value.empty()) {
      return;
    }
    executeRemoteCommand(String(value.c_str()));
  }
};

ServerCallbacks serverCallbacks;
CommandCallbacks commandCallbacks;

void setupBle() {
  NimBLEDevice::init(deviceName);
  NimBLEDevice::setPower(9, NimBLETxPowerType::All);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks, false);

  NimBLEService* service = server->createService(serviceUuid);

  statusChar = service->createCharacteristic(
    statusUuid,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  statusChar->setValue("boot");

  NimBLECharacteristic* cmdChar = service->createCharacteristic(
    commandUuid,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  cmdChar->setCallbacks(&commandCallbacks);

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->enableScanResponse(true);
  advertising->setMinInterval(32);
  advertising->setMaxInterval(64);
  advertising->addServiceUUID(serviceUuid);
  advertising->setName(deviceName);
  advertising->addTxPower();
  advertising->start();

  Serial.println("BLE advertising as Fusor-Valve");
}

void setup() {
  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
  pinMode(enablePin, OUTPUT);

  digitalWrite(stepPin, HIGH);
  digitalWrite(dirPin, LOW);
  setDriverEnabled(false);

  Serial.begin(115200);
  delay(1500);

  prefs.begin("valve", false);
  positionSteps = prefs.getLong("pos", 0);
  openLimitSteps = prefs.getLong("open", 0);
  currentMode = static_cast<ValveMode>(prefs.getUChar("mode", MODE_CALIBRATE));

  if (currentMode == MODE_RUN) {
    long clamped = clampTarget(positionSteps);
    if (clamped != positionSteps) {
      positionSteps = clamped;
      savePosition();
    }
  }

  setupBle();
  sendStatus();
  printBanner();
  printHelp();
}

void loop() {
  static int escState = 0;
  static bool buffering = false;
  static String lineBuffer;

  while (Serial.available()) {
    char c = Serial.read();

    if (buffering) {
      if (c == '\n' || c == '\r') {
        Serial.println();
        if (lineBuffer.length() > 0) {
          executeRemoteCommand(lineBuffer);
        }
        lineBuffer = "";
        buffering = false;
      } else if (c == 27) {
        Serial.println(" [cancelled]");
        lineBuffer = "";
        buffering = false;
      } else if (c == 8 || c == 127) {
        if (lineBuffer.length() > 0) {
          lineBuffer.remove(lineBuffer.length() - 1);
          Serial.print("\b \b");
        }
      } else {
        lineBuffer += c;
        Serial.print(c);
      }
      continue;
    }

    if (escState == 0) {
      if (c == 27) {
        escState = 1;
      } else if (c == ':') {
        buffering = true;
        lineBuffer = "";
        Serial.print(':');
      } else {
        handleChar(c);
      }
    } else if (escState == 1) {
      escState = c == '[' ? 2 : 0;
    } else if (escState == 2) {
      if (c == 'D') {
        jog(false);
      } else if (c == 'C') {
        jog(true);
      }
      escState = 0;
    }
  }

  if (millis() - lastStatusSentMs >= heartbeatIntervalMs) {
    sendStatus();
  }
}
