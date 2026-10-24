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
  :root {
    color-scheme: dark;
    --bg: #14171c;
    --fg: #f2f3f5;
    --muted: #9aa3af;
  }
  * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
  body {
    margin: 0;
    min-height: 100vh;
    background: var(--bg);
    color: var(--fg);
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 24px 16px calc(24px + env(safe-area-inset-bottom, 0px));
  }
  h1 { font-size: 1.1rem; font-weight: 600; color: var(--muted); margin: 0 0 24px; letter-spacing: .02em; }
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
    margin-bottom: 28px;
  }
  .speed-wrap label { display: block; margin-bottom: 8px; color: var(--muted); font-size: .9rem; }
  input[type="range"] { width: 100%; }
  #speedVal { font-variant-numeric: tabular-nums; }
  .presets {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 10px;
    width: 100%;
    max-width: 360px;
    margin-bottom: 10px;
  }
  .preset {
    padding: 14px 6px;
    font-size: .95rem;
    border-radius: 12px;
    background: #2a2e37;
    color: var(--fg);
  }
  .presets.editing .preset { outline: 2px dashed var(--muted); outline-offset: 2px; }
  .link-btn {
    background: none;
    border: none;
    padding: 6px 4px;
    font-size: .8rem;
    font-weight: 500;
    color: var(--muted);
    text-decoration: underline;
    margin-bottom: 28px;
  }
  .panel {
    width: 100%;
    max-width: 360px;
    text-align: center;
    padding-top: 16px;
    border-top: 1px solid #ffffff1a;
  }
  .panel label { display: block; margin-bottom: 10px; color: var(--muted); font-size: .9rem; }
  .swatches { display: flex; justify-content: center; gap: 10px; margin-bottom: 10px; }
  .swatch {
    width: 32px;
    height: 32px;
    padding: 0;
    border-radius: 50%;
    border: 2px solid transparent;
  }
  .swatch.active { border-color: var(--fg); }
  #bgPicker {
    width: 56px;
    height: 32px;
    padding: 0;
    border: none;
    border-radius: 8px;
    background: none;
    cursor: pointer;
  }
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

  <div class="presets" id="presets">
    <button class="preset" data-idx="0">25%</button>
    <button class="preset" data-idx="1">50%</button>
    <button class="preset" data-idx="2">75%</button>
    <button class="preset" data-idx="3">100%</button>
  </div>
  <button class="link-btn" id="editPresetsBtn">&#9998; Edit presets</button>

  <div class="panel">
    <label>Background</label>
    <div class="swatches" id="swatches">
      <button class="swatch" data-color="#14171c" style="background:#14171c" aria-label="Slate"></button>
      <button class="swatch" data-color="#0f172a" style="background:#0f172a" aria-label="Navy"></button>
      <button class="swatch" data-color="#1b2e1f" style="background:#1b2e1f" aria-label="Forest"></button>
      <button class="swatch" data-color="#3a1414" style="background:#3a1414" aria-label="Maroon"></button>
      <button class="swatch" data-color="#f4f4f5" style="background:#f4f4f5" aria-label="Light"></button>
    </div>
    <input type="color" id="bgPicker" value="#14171c">
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

  // ---------------------------------------------------------------
  // Preset speed buttons (saved on this device via localStorage)
  // ---------------------------------------------------------------
  const DEFAULT_PRESETS = [25, 50, 75, 100];
  const presetButtons = Array.from(document.querySelectorAll('.preset'));
  const presetsEl = document.getElementById('presets');
  const editPresetsBtn = document.getElementById('editPresetsBtn');
  let editingPresets = false;

  function clampPct(n) {
    n = Math.round(Number(n));
    if (isNaN(n)) n = 0;
    return Math.max(0, Math.min(100, n));
  }

  function loadPresets() {
    try {
      const saved = JSON.parse(localStorage.getItem('motorPresets'));
      if (Array.isArray(saved) && saved.length === 4) return saved.map(clampPct);
    } catch (e) { /* ignore -- fall back to defaults */ }
    return DEFAULT_PRESETS.slice();
  }

  let presets = loadPresets();

  function savePresets() {
    try { localStorage.setItem('motorPresets', JSON.stringify(presets)); } catch (e) { /* storage unavailable */ }
  }

  function renderPresets() {
    presetButtons.forEach((btn, i) => { btn.textContent = presets[i] + '%'; });
  }

  function applyPreset(i) {
    speedInput.value = presets[i];
    speedVal.textContent = presets[i];
    // If the motor is already running, push the new speed immediately
    // instead of waiting for the next heartbeat tick.
    if (activeDir) {
      const path = activeDir === 'fwd' ? '/forward' : '/reverse';
      statusEl.textContent = (activeDir === 'fwd' ? 'forward' : 'reverse') + ' @ ' + presets[i] + '%';
      send(path + '?speed=' + pct255());
    }
  }

  presetButtons.forEach((btn, i) => {
    btn.addEventListener('click', () => {
      if (editingPresets) {
        const val = prompt('Preset ' + (i + 1) + ' speed (0-100%):', presets[i]);
        if (val !== null) {
          presets[i] = clampPct(val);
          savePresets();
          renderPresets();
        }
      } else {
        applyPreset(i);
      }
    });
  });

  editPresetsBtn.addEventListener('click', () => {
    editingPresets = !editingPresets;
    editPresetsBtn.textContent = editingPresets ? 'Done' : '✎ Edit presets';
    presetsEl.classList.toggle('editing', editingPresets);
  });

  renderPresets();

  // ---------------------------------------------------------------
  // Background color (saved on this device via localStorage)
  // ---------------------------------------------------------------
  const bgPicker = document.getElementById('bgPicker');
  const swatchButtons = Array.from(document.querySelectorAll('.swatch'));

  function relativeLuminance(hex) {
    const c = hex.replace('#', '');
    const toLin = (v) => { v /= 255; return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); };
    const r = toLin(parseInt(c.substring(0, 2), 16));
    const g = toLin(parseInt(c.substring(2, 4), 16));
    const b = toLin(parseInt(c.substring(4, 6), 16));
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
  }

  function applyBackground(hex) {
    const root = document.documentElement.style;
    root.setProperty('--bg', hex);
    const isLight = relativeLuminance(hex) > 0.4;
    root.setProperty('--fg', isLight ? '#14171c' : '#f2f3f5');
    root.setProperty('--muted', isLight ? '#52525b' : '#9aa3af');
    bgPicker.value = hex;
    swatchButtons.forEach((s) => s.classList.toggle('active', s.dataset.color.toLowerCase() === hex.toLowerCase()));
    try { localStorage.setItem('motorBg', hex); } catch (e) { /* storage unavailable */ }
  }

  swatchButtons.forEach((btn) => btn.addEventListener('click', () => applyBackground(btn.dataset.color)));
  bgPicker.addEventListener('input', () => applyBackground(bgPicker.value));

  let savedBg = '#14171c';
  try { savedBg = localStorage.getItem('motorBg') || savedBg; } catch (e) { /* storage unavailable */ }
  applyBackground(savedBg);
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
