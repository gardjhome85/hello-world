/*
 * ESP32 Wireless 12V DC Motor Control
 * ------------------------------------
 * Drives a 12V DC motor through an IBT-2 (BTS7960) H-bridge module.
 * The ESP32 creates its own WiFi hotspot and serves a mobile-friendly
 * web page with Forward / Reverse / Stop controls and a speed slider.
 * No app install is required -- just connect a phone or laptop to the
 * ESP32's WiFi network and open the page in a browser.
 *
 * Hardware: any ESP32 dev board + IBT-2 / BTS7960 motor driver module.
 *
 * Wiring (see README.md for full details and a wiring diagram):
 *   ESP32 GPIO27  -> IBT-2 RPWM
 *   ESP32 GPIO26  -> IBT-2 LPWM
 *   ESP32 GPIO25  -> IBT-2 R_EN + L_EN (tied together)
 *   ESP32 3V3     -> IBT-2 VCC
 *   ESP32 GND     -> IBT-2 GND  -> 12V supply GND (common ground!)
 *   12V supply +  -> IBT-2 B+
 *   12V supply -  -> IBT-2 B-
 *   Motor         -> IBT-2 M+ / M-
 *
 * Safety: the motor is stopped automatically if no command is received
 * from the browser for more than COMMAND_TIMEOUT_MS (e.g. the phone
 * walks out of WiFi range, the tab is closed, etc).
 */

#include <WiFi.h>
#include <WebServer.h>

// ---------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------

// WiFi hotspot credentials (the ESP32 creates this network itself).
// WPA2 passwords must be at least 8 characters.
const char *AP_SSID = "ESP32-Motor";
const char *AP_PASSWORD = "motor1234";

// Motor driver pins (IBT-2 / BTS7960)
const int PIN_RPWM = 27;   // forward PWM
const int PIN_LPWM = 26;   // reverse PWM
const int PIN_ENABLE = 25; // tied to R_EN + L_EN on the IBT-2

// PWM properties
const int PWM_FREQ_HZ = 20000; // 20 kHz -- above audible range
const int PWM_RESOLUTION_BITS = 8; // duty cycle 0-255

// If no command arrives within this many milliseconds while the motor
// is running, the motor is stopped automatically (fail-safe).
const unsigned long COMMAND_TIMEOUT_MS = 800;

// ---------------------------------------------------------------------

WebServer server(80);

int currentSpeed = 0;      // 0-255
int currentDirection = 0;  // -1 = reverse, 0 = stopped, 1 = forward
unsigned long lastCommandMs = 0;

const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>ESP32 Motor Control</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
  body {
    margin: 0;
    min-height: 100vh;
    background: #14171c;
    color: #f2f3f5;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 24px 16px calc(24px + env(safe-area-inset-bottom, 0px));
  }
  h1 { font-size: 1.1rem; font-weight: 600; color: #9aa3af; margin: 0 0 24px; letter-spacing: .02em; }
  #status {
    font-size: .9rem;
    color: #6ee7a7;
    margin-bottom: 20px;
    min-height: 1.2em;
  }
  #status.warn { color: #f5a742; }
  .dpad {
    display: grid;
    grid-template-columns: repeat(2, minmax(120px, 1fr));
    gap: 16px;
    width: 100%;
    max-width: 360px;
    margin-bottom: 24px;
  }
  button {
    font-size: 1.1rem;
    font-weight: 600;
    padding: 28px 12px;
    border-radius: 16px;
    border: none;
    color: #fff;
    user-select: none;
    touch-action: manipulation;
  }
  #fwd { background: #2563eb; }
  #rev { background: #7c3aed; }
  #stop {
    grid-column: 1 / -1;
    background: #dc2626;
    padding: 20px 12px;
  }
  button:active { filter: brightness(0.85); transform: scale(0.98); }
  #fwd.active, #rev.active { box-shadow: 0 0 0 3px #ffffffaa inset; }
  .speed-wrap {
    width: 100%;
    max-width: 360px;
    text-align: center;
  }
  .speed-wrap label { display: block; margin-bottom: 8px; color: #9aa3af; font-size: .9rem; }
  input[type="range"] { width: 100%; }
  #speedVal { font-variant-numeric: tabular-nums; }
</style>
</head>
<body>
  <h1>ESP32 MOTOR CONTROL</h1>
  <div id="status">stopped</div>

  <div class="dpad">
    <button id="fwd">&#9650; FORWARD</button>
    <button id="rev">&#9660; REVERSE</button>
    <button id="stop">&#9632; STOP</button>
  </div>

  <div class="speed-wrap">
    <label>Speed: <span id="speedVal">60</span>%</label>
    <input type="range" id="speed" min="0" max="100" value="60">
  </div>

<script>
  const statusEl = document.getElementById('status');
  const speedInput = document.getElementById('speed');
  const speedVal = document.getElementById('speedVal');
  let heartbeat = null;
  let activeDir = null; // 'fwd' | 'rev' | null

  speedInput.addEventListener('input', () => {
    speedVal.textContent = speedInput.value;
  });

  function pct255() {
    return Math.round(speedInput.value * 255 / 100);
  }

  async function send(path) {
    try {
      await fetch(path, { cache: 'no-store' });
      statusEl.classList.remove('warn');
    } catch (e) {
      statusEl.textContent = 'connection lost';
      statusEl.classList.add('warn');
    }
  }

  const fwdBtn = document.getElementById('fwd');
  const revBtn = document.getElementById('rev');
  const stopBtn = document.getElementById('stop');

  function setActiveButton(btn) {
    fwdBtn.classList.remove('active');
    revBtn.classList.remove('active');
    if (btn) btn.classList.add('active');
  }

  // Click FORWARD or REVERSE to start moving in that direction -- it keeps
  // running (no need to hold the button down) until STOP is pressed or you
  // switch to the other direction.
  function startDirection(dir, btn) {
    activeDir = dir;
    setActiveButton(btn);
    const path = dir === 'fwd' ? '/forward' : '/reverse';
    statusEl.textContent = (dir === 'fwd' ? 'forward' : 'reverse') + ' @ ' + speedInput.value + '%';
    send(path + '?speed=' + pct255());
    clearInterval(heartbeat);
    // Keep re-sending the command on a timer in the background. This both
    // updates speed live as the slider moves and acts as a heartbeat so the
    // ESP32 auto-stops the motor if the page loses its connection.
    heartbeat = setInterval(() => {
      if (activeDir === dir) send(path + '?speed=' + pct255());
    }, 250);
  }

  function stopDirection() {
    setActiveButton(null);
    activeDir = null;
    clearInterval(heartbeat);
    statusEl.textContent = 'stopped';
    send('/stop');
  }

  fwdBtn.addEventListener('click', () => startDirection('fwd', fwdBtn));
  revBtn.addEventListener('click', () => startDirection('rev', revBtn));
  stopBtn.addEventListener('click', stopDirection);

  // Safety: since the motor now keeps running after a single tap, stop it
  // if the page is hidden/backgrounded or closed so it can't run away
  // unattended (the 250ms heartbeat above also stops it if WiFi drops).
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) stopDirection();
  });
  window.addEventListener('pagehide', () => stopDirection());
</script>
</body>
</html>
)HTML";

// ---------------------------------------------------------------------
// Motor driver helpers
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
// Web server handlers
// ---------------------------------------------------------------------

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleForward() {
  int speed = currentSpeed;
  if (server.hasArg("speed")) speed = server.arg("speed").toInt();
  motorForward(speed);
  lastCommandMs = millis();
  server.send(200, "text/plain", "OK");
}

void handleReverse() {
  int speed = currentSpeed;
  if (server.hasArg("speed")) speed = server.arg("speed").toInt();
  motorReverse(speed);
  lastCommandMs = millis();
  server.send(200, "text/plain", "OK");
}

void handleStop() {
  motorStop();
  lastCommandMs = millis();
  server.send(200, "text/plain", "OK");
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ---------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  driverInit();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("Access point started. Connect to WiFi \"");
  Serial.print(AP_SSID);
  Serial.println("\" and open http://192.168.4.1/");

  server.on("/", handleRoot);
  server.on("/forward", handleForward);
  server.on("/reverse", handleReverse);
  server.on("/stop", handleStop);
  server.onNotFound(handleNotFound);
  server.begin();

  lastCommandMs = millis();
}

void loop() {
  server.handleClient();

  // Fail-safe: stop the motor if the browser stops sending commands
  // (out of range, page closed, WiFi hiccup, etc).
  if (currentDirection != 0 && (millis() - lastCommandMs > COMMAND_TIMEOUT_MS)) {
    motorStop();
    Serial.println("Command timeout -- motor stopped for safety.");
  }
}
