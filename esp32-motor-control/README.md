# ESP32 Wireless 12V Motor Control

Wirelessly control a 12V DC motor from your phone's browser. The ESP32
drives the motor through an IBT-2 (BTS7960) H-bridge module. Two
interchangeable versions are included — same wiring, same control UI,
different wireless transport:

| | `esp32_motor_control.ino` | `esp32_motor_control_ble.ino` |
|---|---|---|
| Transport | WiFi (ESP32 hosts its own hotspot) | Bluetooth Low Energy |
| Control page | Served by the ESP32 at `192.168.4.1` | `ble_control.html`, opened locally in a Chromium browser |
| Range | Typical WiFi range | Shorter (typical BLE range) |
| Phone's internet | Occupied by the ESP32's hotspot (unless using STA mode below) | Free — BLE doesn't use WiFi |
| Browser support | Any browser | Chrome/Edge/Opera only (Web Bluetooth) — no Safari/iOS |

Both give you: tap **FORWARD** / **REVERSE** to start moving in that
direction — the motor keeps running with no need to hold the button
down — a **STOP** button, and a speed slider (0-100%). The motor stops
automatically if the connection drops or the page is closed/backgrounded.

## Parts list

- ESP32 dev board (any variant — ESP32-DevKitC, WROOM-32, etc.)
- IBT-2 module (BTS7960-based 43A H-bridge driver) — this is what "BT-2"
  usually refers to
- 12V DC motor
- 12V power supply/battery sized for your motor's current draw
- Common ground wiring between the ESP32, the IBT-2 logic side, and the
  12V supply

## Wiring

| IBT-2 pin        | Connects to                                  |
|-------------------|-----------------------------------------------|
| RPWM              | ESP32 GPIO 27                                 |
| LPWM              | ESP32 GPIO 26                                 |
| R_EN + L_EN (tied)| ESP32 GPIO 25                                 |
| VCC               | ESP32 3V3                                     |
| GND               | ESP32 GND **and** 12V supply GND (common ground) |
| B+                | 12V supply +                                  |
| B-                | 12V supply -                                  |
| M+ / M-           | Motor terminals                               |

```
                +-------------------+
                |       ESP32       |
                |                   |
                |  GPIO27 (RPWM) ---|-------> IBT-2 RPWM
                |  GPIO26 (LPWM) ---|-------> IBT-2 LPWM
                |  GPIO25 (EN)   ---|-------> IBT-2 R_EN + L_EN
                |  3V3           ---|-------> IBT-2 VCC
                |  GND           ---|----+--> IBT-2 GND
                +-------------------+    |
                                          |
                12V supply (-) ----------+
                12V supply (+) --------------> IBT-2 B+
                                12V supply (-) -> IBT-2 B-

                IBT-2 M+ / M- -----------------> Motor
```

**Important:** the ESP32's GND, the IBT-2's logic GND, and the 12V
supply's GND must all be tied together (a common ground), or the PWM
signals will be unreliable.

The BTS7960 chips on the IBT-2 already include internal free-wheeling
protection, so no external flyback diode is needed. Still, size your
12V supply and any fuse/wiring for the motor's actual stall current,
and keep the motor's power wiring separate from the ESP32's logic
wiring.

## Flashing the ESP32 — WiFi version

1. In the Arduino IDE, install **esp32 by Espressif Systems** via
   Boards Manager (version 3.0.0 or newer — the sketch uses the newer
   pin-based `ledcAttach()`/`ledcWrite()` API).
   - If you're on the older 2.x core, replace the PWM setup in
     `driverInit()` with the legacy channel-based API:
     ```cpp
     ledcSetup(0, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
     ledcSetup(1, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
     ledcAttachPin(PIN_RPWM, 0);
     ledcAttachPin(PIN_LPWM, 1);
     ```
     and change `ledcWrite(PIN_RPWM, speed)` calls to use the channel
     number (`ledcWrite(0, speed)` / `ledcWrite(1, speed)`) instead of
     the pin number.
2. Open `esp32_motor_control.ino`.
3. Select your ESP32 board and port under **Tools**.
4. (Optional) change `AP_SSID` / `AP_PASSWORD` at the top of the sketch.
   The password must be at least 8 characters.
5. Upload.

### Using it

1. Power on the ESP32 and the 12V supply.
2. On your phone, connect to the WiFi network `ESP32-Motor` (password
   `motor1234`, unless you changed them).
3. Open a browser and go to `http://192.168.4.1/`.
4. Tap **FORWARD** or **REVERSE** to start driving the motor in that
   direction — it keeps running on its own; adjust the slider to change
   speed while it runs; tap **STOP** (or the other direction button) to
   stop or switch direction.

## Flashing the ESP32 — BLE version

1. Same Arduino IDE / esp32 core setup as above (the BLE library ships
   with the esp32 Arduino core, no extra install needed).
2. Open `esp32_motor_control_ble.ino` instead.
3. Select your ESP32 board and port under **Tools**.
4. (Optional) change `BLE_DEVICE_NAME` at the top of the sketch.
5. Upload.

### Using it

**Option A — `ble_control.html` (recommended, same UI as the WiFi version):**

1. Power on the ESP32 and the 12V supply.
2. Copy `ble_control.html` to your phone or computer and open it in a
   **Chromium-based browser** (Chrome, Edge, or Opera — desktop,
   Android, or ChromeOS). Opening the file directly (double-click, or
   `chrome://` file picker) works, since Chrome treats local `file://`
   pages as a secure context for Web Bluetooth. If your Android build
   won't open it directly, serve it from a quick local server instead
   (e.g. `python3 -m http.server` in this folder, then browse to it).
3. Tap **Connect via Bluetooth** and select `ESP32-Motor-BLE` from the
   device picker.
4. Tap **FORWARD** / **REVERSE** / **STOP** and use the speed slider —
   same behavior as the WiFi control page.

   **Not supported on iOS/Safari** — Apple has not implemented Web
   Bluetooth. On iPhone, use Option B below, or a third-party browser
   with Bluetooth support (e.g. Bluefy).

**Option B — a generic BLE app (works on any platform, e.g. iOS):**

1. Install a generic BLE tool such as **nRF Connect for Mobile**
   (iOS/Android).
2. Scan and connect to `ESP32-Motor-BLE`.
3. Open the custom service (UUID `b3fdd1d0-...6b01`) and find the
   command characteristic (UUID `b3fdd1d1-...6b01`).
4. Write these as text (UTF-8) to that characteristic:
   - `F200` — forward at speed 200 (0-255)
   - `R150` — reverse at speed 150
   - `S` — stop
5. Optionally subscribe to notifications on the status characteristic
   (UUID `b3fdd1d2-...6b01`) to see the ESP32's current state
   (`FWD:200`, `REV:150`, or `STOP`).

## Compiling from the command line (optional)

If you prefer `arduino-cli` over the Arduino IDE, each sketch needs to
sit in a folder with the same name as the `.ino` file, so copy them out
first:

```sh
arduino-cli config add board_manager.additional_urls \
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32

mkdir -p build/esp32_motor_control build/esp32_motor_control_ble
cp esp32_motor_control.ino build/esp32_motor_control/
cp esp32_motor_control_ble.ino build/esp32_motor_control_ble/

arduino-cli compile --fqbn esp32:esp32:esp32 build/esp32_motor_control
arduino-cli compile --fqbn esp32:esp32:esp32 build/esp32_motor_control_ble
```

Add `--upload -p <port>` (e.g. `-p /dev/ttyUSB0` or `-p COM3`) to
flash the board in the same step. The BLE sketch is large; if it
reports "Sketch too big", pick a bigger partition scheme with
`--board-options PartitionScheme=huge_app` (or **Tools → Partition
Scheme → Huge APP** in the IDE).

## Switching the WiFi version to your home WiFi instead of a hotspot

If you'd rather have the ESP32 join your existing WiFi network (so you
can control it from any device already on that network) instead of
creating its own hotspot, replace the `setup()` WiFi block with:

```cpp
WiFi.mode(WIFI_STA);
WiFi.begin("YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD");
while (WiFi.status() != WL_CONNECTED) {
  delay(500);
  Serial.print(".");
}
Serial.print("Connected. IP address: ");
Serial.println(WiFi.localIP());
```

Then open the printed IP address in your browser instead of
`192.168.4.1`.
