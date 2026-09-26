/*
 * ESP32 Wireless 12V DC Motor Control -- Bluetooth Low Energy (BLE) version
 * --------------------------------------------------------------------------
 * Drives a 12V DC motor through an IBT-2 (BTS7960) H-bridge module, the
 * same as esp32_motor_control.ino, but controlled over BLE instead of
 * WiFi. Use this if you don't want the ESP32 hosting its own WiFi
 * network, want shorter range / lower power, or want to keep the
 * phone's WiFi free for internet access while driving the motor.
 *
 * Control it with either:
 *   - ble_control.html (in this folder), opened in a Chrome-based
 *     browser (desktop, Android, ChromeOS) -- uses the Web Bluetooth
 *     API to give you the same Forward/Reverse/Stop + speed slider UI
 *     as the WiFi version, no app install needed. NOTE: Web Bluetooth
 *     is not supported in Safari/iOS -- see README.md for iOS options.
 *   - A generic BLE app such as "nRF Connect for Mobile" (iOS/Android),
 *     writing plain-text commands to the command characteristic:
 *       "F200" = forward at speed 200 (0-255)
 *       "R150" = reverse at speed 150
 *       "S"    = stop
 *
 * Hardware and wiring are identical to esp32_motor_control.ino -- see
 * README.md for the wiring table/diagram. Only the control transport
 * (BLE instead of WiFi) and this sketch differ.
 *
 * Safety: the motor is stopped automatically if
 *   (a) no command is received for COMMAND_TIMEOUT_MS, or
 *   (b) the BLE client disconnects.
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ---------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------

const char *BLE_DEVICE_NAME = "ESP32-Motor-BLE";

// Motor driver pins (IBT-2 / BTS7960) -- same as the WiFi version.
const int PIN_RPWM = 27;   // forward PWM
const int PIN_LPWM = 26;   // reverse PWM
const int PIN_ENABLE = 25; // tied to R_EN + L_EN on the IBT-2

// PWM properties
const int PWM_FREQ_HZ = 20000; // 20 kHz -- above audible range
const int PWM_RESOLUTION_BITS = 8; // duty cycle 0-255

// If no command arrives within this many milliseconds while the motor
// is running, the motor is stopped automatically (fail-safe).
const unsigned long COMMAND_TIMEOUT_MS = 800;

// Custom BLE service/characteristic UUIDs (randomly generated -- unique
// to this project, not a standard BLE profile).
#define SERVICE_UUID      "b3fdd1d0-2c47-4bd6-8f8e-1d6d4d2a6b01"
#define COMMAND_CHAR_UUID "b3fdd1d1-2c47-4bd6-8f8e-1d6d4d2a6b01" // write
#define STATUS_CHAR_UUID  "b3fdd1d2-2c47-4bd6-8f8e-1d6d4d2a6b01" // read/notify

// ---------------------------------------------------------------------

BLECharacteristic *statusChar = nullptr;
bool deviceConnected = false;

int currentSpeed = 0;      // 0-255
int currentDirection = 0;  // -1 = reverse, 0 = stopped, 1 = forward
unsigned long lastCommandMs = 0;

// ---------------------------------------------------------------------
// Motor driver helpers (identical to the WiFi version)
// ---------------------------------------------------------------------

void driverInit() {
  pinMode(PIN_ENABLE, OUTPUT);
  digitalWrite(PIN_ENABLE, HIGH); // enable the IBT-2 bridge

  ledcAttach(PIN_RPWM, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
  ledcAttach(PIN_LPWM, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
  ledcWrite(PIN_RPWM, 0);
  ledcWrite(PIN_LPWM, 0);
}

void motorStop() {
  ledcWrite(PIN_RPWM, 0);
  ledcWrite(PIN_LPWM, 0);
  currentDirection = 0;
  currentSpeed = 0;
}

void motorForward(int speed) {
  speed = constrain(speed, 0, 255);
  ledcWrite(PIN_LPWM, 0);
  ledcWrite(PIN_RPWM, speed);
  currentDirection = 1;
  currentSpeed = speed;
}

void motorReverse(int speed) {
  speed = constrain(speed, 0, 255);
  ledcWrite(PIN_RPWM, 0);
  ledcWrite(PIN_LPWM, speed);
  currentDirection = -1;
  currentSpeed = speed;
}

// ---------------------------------------------------------------------
// BLE command handling
// ---------------------------------------------------------------------

void notifyStatus() {
  String msg;
  if (currentDirection == 1) msg = "FWD:" + String(currentSpeed);
  else if (currentDirection == -1) msg = "REV:" + String(currentSpeed);
  else msg = "STOP";

  if (statusChar == nullptr) return;
  statusChar->setValue(msg.c_str());
  if (deviceConnected) statusChar->notify();
}

// Commands are plain ASCII text written to the command characteristic:
//   "F<speed>"  e.g. "F200" -> forward at speed 0-255
//   "R<speed>"  e.g. "R150" -> reverse at speed 0-255
//   "S"         -> stop
void handleCommand(const std::string &cmd) {
  if (cmd.empty()) return;

  char c = cmd[0];
  int speed = currentSpeed;
  if (cmd.length() > 1) {
    speed = atoi(cmd.substr(1).c_str());
  }

  switch (c) {
    case 'F':
    case 'f':
      motorForward(speed);
      break;
    case 'R':
    case 'r':
      motorReverse(speed);
      break;
    case 'S':
    case 's':
      motorStop();
      break;
    default:
      return; // unrecognized command, ignore
  }

  lastCommandMs = millis();
  notifyStatus();
}

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    handleCommand(characteristic->getValue());
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    deviceConnected = true;
    lastCommandMs = millis();
    Serial.println("BLE client connected.");
  }

  void onDisconnect(BLEServer *server) override {
    deviceConnected = false;
    motorStop();
    Serial.println("BLE client disconnected -- motor stopped.");
    delay(200); // give the BLE stack a moment before re-advertising
    BLEDevice::startAdvertising();
  }
};

// ---------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  driverInit();

  BLEDevice::init(BLE_DEVICE_NAME);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService *service = server->createService(SERVICE_UUID);

  BLECharacteristic *commandChar = service->createCharacteristic(
      COMMAND_CHAR_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  commandChar->setCallbacks(new CommandCallbacks());

  statusChar = service->createCharacteristic(
      STATUS_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  statusChar->addDescriptor(new BLE2902());
  statusChar->setValue("STOP");

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.print("BLE advertising as \"");
  Serial.print(BLE_DEVICE_NAME);
  Serial.println("\". Connect with ble_control.html or a BLE app.");

  lastCommandMs = millis();
}

void loop() {
  // Fail-safe: stop the motor if the connected client stops sending
  // commands (app backgrounded without disconnecting, etc).
  if (currentDirection != 0 && (millis() - lastCommandMs > COMMAND_TIMEOUT_MS)) {
    motorStop();
    notifyStatus();
    Serial.println("Command timeout -- motor stopped for safety.");
  }
}
