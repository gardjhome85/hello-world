# ESP32 Wireless 12V Motor Control

Wirelessly control a 12V DC motor from your phone's browser. The ESP32
runs an IBT-2 (BTS7960) H-bridge motor driver and hosts its own WiFi
hotspot with a mobile-friendly control page — **no app install required**.

Controls: tap **FORWARD** / **REVERSE** to start moving in that
direction — the motor keeps running with no need to hold the button
down — a **STOP** button, and a speed slider (0-100%). The motor stops
automatically if the phone loses the connection or the page is
closed/backgrounded.

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

## Flashing the ESP32

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

## Using it

1. Power on the ESP32 and the 12V supply.
2. On your phone, connect to the WiFi network `ESP32-Motor` (password
   `motor1234`, unless you changed them).
3. Open a browser and go to `http://192.168.4.1/`.
4. Tap **FORWARD** or **REVERSE** to start driving the motor in that
   direction — it keeps running on its own; adjust the slider to change
   speed while it runs; tap **STOP** (or the other direction button) to
   stop or switch direction.

## Notes on the "app"

This uses a **web app** served directly by the ESP32 rather than a
native phone app — you get a real control UI on iOS and Android with
zero installation, and it's simpler and more reliable than building and
distributing a native app. If you'd rather have a Bluetooth (BLE) based
control instead of WiFi (e.g. to also connect to your phone's internet
while driving the motor), that's a different sketch — let me know and
I can put that together too.

## Switching to your home WiFi instead of a hotspot

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
