#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>

// Required for brownout bypass and clean GPIO pad reset
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/gpio.h"

// =============================
// WI-FI CONFIGURATION
// =============================
const char* AP_SSID = "Smart_Parking_Hub";
const char* AP_PASS = "12345678";

WebServer server(80);

// =============================
// LCD
// =============================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// =============================
// SERVOS
// =============================
Servo entryServo;
Servo exitServo;

// =============================
// PERSISTENT STORAGE (NVS Flash)
// =============================
Preferences prefs;

// =============================
// WIRING
// =============================
#define ENTRY_IR_PIN     26
#define EXIT_IR_PIN      25

#define ENTRY_SERVO_PIN  14
#define EXIT_SERVO_PIN   13

// =============================
// PARKING SETTINGS
// =============================
const int TOTAL_SLOTS = 20;

int occupied_slots = 0;
int available_slots = TOTAL_SLOTS;

// =============================
// SERVO POSITIONS & TIMING
// =============================
const int CLOSED_ANGLE = 0;
const int OPEN_ANGLE   = 90;

const unsigned long GATE_TIME = 3000;   // Auto-close duration (ms)

unsigned long entryStartTime = 0;
unsigned long exitStartTime  = 0;

bool entryGateOpen   = false;
bool exitGateOpen    = false;
bool entryAutoClose  = true;  // false if user clicked 'Hold Open'
bool exitAutoClose   = true;  // false if user clicked 'Hold Open'

// =============================
// IR SENSOR DEBOUNCE STATE
// =============================
struct IRSensor {
  int pin;
  int stableState;
  int lastRawState;
  unsigned long lastChangeTime;
  bool processed;
};

const unsigned long DEBOUNCE_TIME = 50; // ms

IRSensor entryIR = { ENTRY_IR_PIN, HIGH, HIGH, 0, false };
IRSensor exitIR  = { EXIT_IR_PIN,  HIGH, HIGH, 0, false };

// =============================
// LCD CHANGE TRACKING & MESSAGES
// =============================
bool messageActive = false;
unsigned long messageStartTime = 0;
const unsigned long MESSAGE_TIME = 1500;

int lastPrintedOccupied  = -1;
int lastPrintedAvailable = -1;

// =============================
// EMBEDDED DEFAULT WEB ASSETS (AUTO-SEEDED TO LITTLEFS IF MISSING)
// =============================
const char DEFAULT_INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Smart Parking Control Center</title>
  <link rel="stylesheet" href="style.css">
</head>
<body>
  <div class="container">
    <header>
      <div class="title-row">
        <h1>🚗 Smart Parking Control</h1>
        <div id="conn-status" class="status-pill pill-online">
          <span class="dot"></span> <span id="conn-text">ONLINE</span>
        </div>
      </div>
      <p class="subtitle">ESP32 Live Telemetry & Barrier Gate Remote</p>
      <div id="ip-config-bar" class="ip-bar">
        <label for="esp-ip">Device IP:</label>
        <input type="text" id="esp-ip" value="http://192.168.4.1">
        <button id="save-ip-btn" class="btn btn-secondary">Connect</button>
      </div>
    </header>

    <div id="banner" class="status-banner banner-ok">CHECKING SYSTEM...</div>

    <div class="grid grid-3">
      <div class="card metric-card">
        <div class="label">Available</div>
        <div id="avail" class="value val-avail">--</div>
      </div>
      <div class="card metric-card">
        <div class="label">Occupied</div>
        <div id="occ" class="value val-occ">--</div>
      </div>
      <div class="card metric-card">
        <div class="label">Capacity</div>
        <div id="total" class="value val-total">--</div>
      </div>
    </div>

    <div class="card progress-card">
      <div class="progress-header">
        <span>Occupancy Ratio</span>
        <span id="cap-text">0 / 20 Slots</span>
      </div>
      <div class="progress-track">
        <div id="bar" class="progress-fill"></div>
      </div>
    </div>

    <section class="section-title">Manual Gate Actuators</section>
    <div class="grid grid-2">
      <div class="card gate-card">
        <div class="gate-header">
          <h3>Entry Barrier</h3>
          <span id="entry-badge" class="badge badge-closed">CLOSED</span>
        </div>
        <p class="gate-hint">GPIO 14 Servo (Sensor: GPIO 26)</p>
        <div class="button-group">
          <button class="btn btn-primary" onclick="actuateGate('entry', 'pulse')">🟢 Open (3s Pulse)</button>
          <div class="btn-subgroup">
            <button class="btn btn-outline" onclick="actuateGate('entry', 'open')">Hold Open</button>
            <button class="btn btn-outline btn-danger" onclick="actuateGate('entry', 'close')">Close</button>
          </div>
        </div>
      </div>

      <div class="card gate-card">
        <div class="gate-header">
          <h3>Exit Barrier</h3>
          <span id="exit-badge" class="badge badge-closed">CLOSED</span>
        </div>
        <p class="gate-hint">GPIO 13 Servo (Sensor: GPIO 25)</p>
        <div class="button-group">
          <button class="btn btn-primary" onclick="actuateGate('exit', 'pulse')">🟢 Open (3s Pulse)</button>
          <div class="btn-subgroup">
            <button class="btn btn-outline" onclick="actuateGate('exit', 'open')">Hold Open</button>
            <button class="btn btn-outline btn-danger" onclick="actuateGate('exit', 'close')">Close</button>
          </div>
        </div>
      </div>
    </div>

    <section class="section-title">Slot Counter Management</section>
    <div class="card control-card">
      <div class="button-row">
        <button class="btn btn-secondary" onclick="adjustSlots('inc')">➕ Manual Entry (+1)</button>
        <button class="btn btn-secondary" onclick="adjustSlots('dec')">➖ Manual Exit (-1)</button>
        <button class="btn btn-danger" onclick="resetSlots()">🔄 Reset All (0)</button>
      </div>
    </div>

    <section class="section-title">Live System Logs</section>
    <div class="card log-card">
      <ul id="event-log">
        <li class="log-entry">Waiting for connection...</li>
      </ul>
    </div>
  </div>
  <script src="app.js"></script>
</body>
</html>)rawliteral";

const char DEFAULT_STYLE_CSS[] PROGMEM = R"rawliteral(:root {
  --bg: #090d16;
  --card-bg: #131c2e;
  --card-border: #1e293b;
  --text: #f1f5f9;
  --muted: #94a3b8;
  --green: #10b981;
  --green-glow: rgba(16, 185, 129, 0.2);
  --red: #ef4444;
  --red-glow: rgba(239, 68, 68, 0.2);
  --amber: #f59e0b;
  --blue: #38bdf8;
  --blue-hover: #0284c7;
}
* { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
body { background: var(--bg); color: var(--text); padding: 24px 16px; display: flex; justify-content: center; min-height: 100vh; }
.container { width: 100%; max-width: 480px; }
header { margin-bottom: 20px; }
.title-row { display: flex; justify-content: space-between; align-items: center; margin-bottom: 4px; }
h1 { font-size: 1.45rem; font-weight: 700; }
.subtitle { font-size: 0.85rem; color: var(--muted); }
.status-pill { display: inline-flex; align-items: center; gap: 6px; padding: 4px 10px; border-radius: 999px; font-size: 0.75rem; font-weight: 700; }
.pill-online { background: var(--green-glow); color: var(--green); }
.pill-offline { background: var(--red-glow); color: var(--red); }
.dot { width: 7px; height: 7px; border-radius: 50%; background: currentColor; animation: pulse 1.6s infinite; }
@keyframes pulse { 0%, 100% { opacity: 1; transform: scale(1); } 50% { opacity: 0.4; transform: scale(0.85); } }
.ip-bar { display: none; margin-top: 10px; padding: 8px 12px; background: var(--card-bg); border: 1px solid var(--card-border); border-radius: 10px; align-items: center; gap: 8px; font-size: 0.8rem; }
.ip-bar label { color: var(--muted); font-weight: 600; }
.ip-bar input { flex: 1; background: #0f172a; border: 1px solid #334155; color: #fff; padding: 6px 10px; border-radius: 6px; }
.status-banner { text-align: center; padding: 12px; border-radius: 12px; font-weight: 700; font-size: 0.95rem; margin-bottom: 16px; }
.banner-ok { background: var(--green-glow); color: var(--green); border: 1px solid var(--green); }
.banner-full { background: var(--red-glow); color: var(--red); border: 1px solid var(--red); }
.grid { display: grid; gap: 12px; margin-bottom: 16px; }
.grid-3 { grid-template-columns: repeat(3, 1fr); }
.grid-2 { grid-template-columns: repeat(2, 1fr); }
.card { background: var(--card-bg); border: 1px solid var(--card-border); border-radius: 14px; padding: 16px; box-shadow: 0 4px 16px rgba(0, 0, 0, 0.3); }
.metric-card { text-align: center; padding: 14px 8px; }
.metric-card .label { font-size: 0.75rem; color: var(--muted); text-transform: uppercase; font-weight: 600; }
.metric-card .value { font-size: 1.8rem; font-weight: 800; margin-top: 4px; }
.val-avail { color: var(--green); }
.val-occ { color: var(--amber); }
.val-total { color: var(--blue); }
.progress-card { margin-bottom: 16px; }
.progress-header { display: flex; justify-content: space-between; font-size: 0.82rem; color: var(--muted); margin-bottom: 8px; }
.progress-track { width: 100%; height: 10px; background: #1e293b; border-radius: 999px; overflow: hidden; }
.progress-fill { height: 100%; background: linear-gradient(90deg, #38bdf8, #818cf8); width: 0%; transition: width 0.4s ease; }
.section-title { font-size: 0.85rem; font-weight: 700; color: var(--muted); text-transform: uppercase; letter-spacing: 0.5px; margin: 18px 0 10px 4px; }
.gate-card { display: flex; flex-direction: column; justify-content: space-between; }
.gate-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 4px; }
.gate-header h3 { font-size: 1rem; font-weight: 700; }
.gate-hint { font-size: 0.72rem; color: var(--muted); margin-bottom: 12px; }
.badge { padding: 3px 8px; border-radius: 6px; font-size: 0.72rem; font-weight: 800; }
.badge-open { background: var(--green-glow); color: var(--green); border: 1px solid var(--green); }
.badge-closed { background: #1e293b; color: var(--muted); border: 1px solid #334155; }
.button-group { display: flex; flex-direction: column; gap: 8px; }
.btn-subgroup { display: grid; grid-template-columns: 1fr 1fr; gap: 6px; }
.button-row { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 8px; }
.btn { border: none; border-radius: 8px; padding: 9px 12px; font-size: 0.82rem; font-weight: 600; cursor: pointer; transition: background 0.15s ease; }
.btn:active { transform: scale(0.97); }
.btn-primary { background: var(--blue); color: #0f172a; font-weight: 700; }
.btn-primary:hover { background: var(--blue-hover); }
.btn-secondary { background: #1e293b; color: #f1f5f9; border: 1px solid #334155; }
.btn-secondary:hover { background: #334155; }
.btn-outline { background: transparent; color: var(--text); border: 1px solid #334155; }
.btn-outline:hover { background: #1e293b; }
.btn-danger { color: var(--red); border-color: rgba(239, 68, 68, 0.4); }
.btn-danger:hover { background: var(--red-glow); }
.log-card { max-height: 120px; overflow-y: auto; padding: 10px 14px; background: #0d131f; }
#event-log { list-style: none; font-family: monospace; font-size: 0.75rem; color: var(--muted); }
.log-entry { padding: 3px 0; border-bottom: 1px solid rgba(255, 255, 255, 0.04); }
.log-entry:first-child { color: #38bdf8; })rawliteral";

const char DEFAULT_APP_JS[] PROGMEM = R"rawliteral(let baseUrl = "";
let lastStatus = null;

function initBaseUrl() {
  const isLocalFile = window.location.protocol === "file:";
  const isExternalHost = !["192.168.4.1", "localhost", "127.0.0.1"].includes(window.location.hostname) && window.location.port !== "";
  const ipBar = document.getElementById("ip-config-bar");
  const ipInput = document.getElementById("esp-ip");
  const saveBtn = document.getElementById("save-ip-btn");
  const savedIp = localStorage.getItem("smartpark_ip") || "http://192.168.4.1";
  ipInput.value = savedIp;

  if (isLocalFile || isExternalHost) {
    ipBar.style.display = "flex";
    baseUrl = savedIp.replace(/\/+$/, "");
  } else {
    baseUrl = "";
  }

  saveBtn.addEventListener("click", () => {
    let val = ipInput.value.trim().replace(/\/+$/, "");
    if (!val.startsWith("http://") && !val.startsWith("https://")) val = "http://" + val;
    ipInput.value = val;
    localStorage.setItem("smartpark_ip", val);
    baseUrl = val;
    logEvent(`Connected target set to ${baseUrl}`);
    fetchStatus();
  });
}

function logEvent(msg) {
  const logEl = document.getElementById("event-log");
  const time = new Date().toLocaleTimeString();
  const li = document.createElement("li");
  li.className = "log-entry";
  li.textContent = `[${time}] ${msg}`;
  logEl.insertBefore(li, logEl.firstChild);
  while (logEl.children.length > 20) logEl.removeChild(logEl.lastChild);
}

async function fetchStatus() {
  const connPill = document.getElementById("conn-status");
  const connText = document.getElementById("conn-text");
  try {
    const res = await fetch(`${baseUrl}/status`, { cache: "no-store" });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();

    connPill.className = "status-pill pill-online";
    connText.textContent = "ONLINE";

    document.getElementById("avail").textContent = data.available;
    document.getElementById("occ").textContent = data.occupied;
    document.getElementById("total").textContent = data.total;
    document.getElementById("cap-text").textContent = `${data.occupied} / ${data.total} Slots`;

    const pct = Math.min(100, Math.round((data.occupied / data.total) * 100));
    document.getElementById("bar").style.width = pct + "%";

    const banner = document.getElementById("banner");
    if (data.available > 0) {
      banner.textContent = "SLOTS AVAILABLE — READY FOR ENTRY";
      banner.className = "status-banner banner-ok";
    } else {
      banner.textContent = "PARKING FULL — ALL SLOTS OCCUPIED";
      banner.className = "status-banner banner-full";
    }

    const entryBadge = document.getElementById("entry-badge");
    entryBadge.textContent = data.entryOpen ? "OPEN" : "CLOSED";
    entryBadge.className = "badge " + (data.entryOpen ? "badge-open" : "badge-closed");

    const exitBadge = document.getElementById("exit-badge");
    exitBadge.textContent = data.exitOpen ? "OPEN" : "CLOSED";
    exitBadge.className = "badge " + (data.exitOpen ? "badge-open" : "badge-closed");

    if (lastStatus) {
      if (!lastStatus.entryOpen && data.entryOpen) logEvent("Entry barrier raised");
      if (lastStatus.entryOpen && !data.entryOpen) logEvent("Entry barrier lowered");
      if (!lastStatus.exitOpen && data.exitOpen) logEvent("Exit barrier raised");
      if (lastStatus.exitOpen && !data.exitOpen) logEvent("Exit barrier lowered");
      if (lastStatus.occupied !== data.occupied) {
        logEvent(`Occupancy: ${data.occupied}/${data.total} occupied`);
      }
    }
    lastStatus = data;
  } catch (err) {
    connPill.className = "status-pill pill-offline";
    connText.textContent = "OFFLINE";
    document.getElementById("banner").textContent = "DISCONNECTED FROM ESP32";
    document.getElementById("banner").className = "status-banner banner-full";
  }
}

async function actuateGate(gate, action) {
  logEvent(`Command: ${gate.toUpperCase()} -> ${action.toUpperCase()}`);
  try {
    const res = await fetch(`${baseUrl}/api/gate/${gate}?action=${action}`, { cache: "no-store" });
    const text = await res.text();
    logEvent(`Result: ${text}`);
    fetchStatus();
  } catch (err) {
    logEvent(`Error sending ${gate} command`);
  }
}

async function adjustSlots(action) {
  logEvent(`Slot update: ${action}`);
  try {
    const res = await fetch(`${baseUrl}/api/slots?action=${action}`, { cache: "no-store" });
    const text = await res.text();
    logEvent(`Result: ${text}`);
    fetchStatus();
  } catch (err) {
    logEvent("Error adjusting slot count");
  }
}

async function resetSlots() {
  if (!confirm("Are you sure you want to reset all slots to 0 occupied?")) return;
  logEvent("Resetting parking slots to 0...");
  try {
    const res = await fetch(`${baseUrl}/reset`, { cache: "no-store" });
    const text = await res.text();
    logEvent(`Result: ${text}`);
    fetchStatus();
  } catch (err) {
    logEvent("Error resetting slots");
  }
}

document.addEventListener("DOMContentLoaded", () => {
  initBaseUrl();
  logEvent("Web Controller initialized");
  fetchStatus();
  setInterval(fetchStatus, 1000);
});)rawliteral";

// =============================
// DEBOUNCED READ
// =============================
bool debouncedFallingEdge(IRSensor &s) {
  int raw = digitalRead(s.pin);

  if (raw != s.lastRawState) {
    s.lastChangeTime = millis();
    s.lastRawState = raw;
  }

  if ((millis() - s.lastChangeTime) > DEBOUNCE_TIME) {
    if (s.stableState != raw) {
      s.stableState = raw;

      if (s.stableState == LOW && !s.processed) {
        s.processed = true;
        return true;
      }
    }
  }

  if (s.stableState == HIGH) {
    s.processed = false;
  }

  return false;
}

// =============================
// LCD DISPLAY
// =============================
void updateLCD() {
  if (occupied_slots == lastPrintedOccupied &&
      available_slots == lastPrintedAvailable) {
    return;
  }

  lcd.setCursor(0, 0);
  lcd.print("Occupied: ");
  lcd.print(occupied_slots);
  lcd.print("   ");

  lcd.setCursor(0, 1);
  lcd.print("Available: ");
  lcd.print(available_slots);
  lcd.print("   ");

  lastPrintedOccupied = occupied_slots;
  lastPrintedAvailable = available_slots;
}

void showTempMessage(const char* line0, const char* line1) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line0);
  lcd.setCursor(0, 1);
  lcd.print(line1);

  messageActive = true;
  messageStartTime = millis();

  lastPrintedOccupied = -1;
  lastPrintedAvailable = -1;
}

void handleTempMessage() {
  if (messageActive && (millis() - messageStartTime >= MESSAGE_TIME)) {
    messageActive = false;
    lcd.clear();
    updateLCD();
  }
}

// =============================
// NVS FLASH PERSISTENCE
// =============================
void saveSlotCount() {
  prefs.putInt("occupied", occupied_slots);
}

void loadSlotCount() {
  occupied_slots = prefs.getInt("occupied", 0);
  if (occupied_slots < 0) occupied_slots = 0;
  if (occupied_slots > TOTAL_SLOTS) occupied_slots = TOTAL_SLOTS;
  available_slots = TOTAL_SLOTS - occupied_slots;
}

// =============================
// GATE ACTUATION
// =============================
void openGate(Servo &servo, bool &gateOpenFlag, unsigned long &startTime, bool autoClose = true) {
  servo.write(OPEN_ANGLE);
  gateOpenFlag = true;
  startTime = millis();
  if (&servo == &entryServo) entryAutoClose = autoClose;
  if (&servo == &exitServo)  exitAutoClose  = autoClose;
}

void closeGate(Servo &servo, bool &gateOpenFlag, const char* label) {
  servo.write(CLOSED_ANGLE);
  gateOpenFlag = false;
  Serial.print(">>> ");
  Serial.print(label);
  Serial.println(" SERVO CLOSED");
}

void closeGateIfDue(Servo &servo, bool &gateOpenFlag, unsigned long startTime, bool autoClose, const char* label) {
  if (gateOpenFlag && autoClose && (millis() - startTime >= GATE_TIME)) {
    closeGate(servo, gateOpenFlag, label);
  }
}

// =============================
// LITTLEFS FILESYSTEM UTILS
// =============================
void setupLittleFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("[FS] LittleFS Mount Failed");
    return;
  }
  Serial.println("[FS] LittleFS mounted successfully");

  // Auto-seed files if LittleFS partition is fresh/empty
  if (!LittleFS.exists("/index.html")) {
    Serial.println("[FS] Seeding default /index.html...");
    File f = LittleFS.open("/index.html", "w");
    if (f) { f.print(DEFAULT_INDEX_HTML); f.close(); }
  }
  if (!LittleFS.exists("/style.css")) {
    Serial.println("[FS] Seeding default /style.css...");
    File f = LittleFS.open("/style.css", "w");
    if (f) { f.print(DEFAULT_STYLE_CSS); f.close(); }
  }
  if (!LittleFS.exists("/app.js")) {
    Serial.println("[FS] Seeding default /app.js...");
    File f = LittleFS.open("/app.js", "w");
    if (f) { f.print(DEFAULT_APP_JS); f.close(); }
  }
}

// =============================
// WEB SERVER HANDLERS
// =============================
void sendCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");
}

void handleFileRead(const char* path, const char* contentType) {
  sendCORS();
  if (LittleFS.exists(path)) {
    File file = LittleFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
  } else {
    // Fallback to embedded flash assets
    if (strcmp(path, "/index.html") == 0) server.send_P(200, "text/html", DEFAULT_INDEX_HTML);
    else if (strcmp(path, "/style.css") == 0) server.send_P(200, "text/css", DEFAULT_STYLE_CSS);
    else if (strcmp(path, "/app.js") == 0) server.send_P(200, "application/javascript", DEFAULT_APP_JS);
    else server.send(404, "text/plain", "File Not Found");
  }
}

void handleStatus() {
  sendCORS();
  String json = "{";
  json += "\"total\":" + String(TOTAL_SLOTS) + ",";
  json += "\"occupied\":" + String(occupied_slots) + ",";
  json += "\"available\":" + String(available_slots) + ",";
  json += "\"entryOpen\":" + String(entryGateOpen ? "true" : "false") + ",";
  json += "\"exitOpen\":" + String(exitGateOpen ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

// Gate API: /api/gate/entry?action=pulse|open|close
void handleGateEntry() {
  sendCORS();
  String action = server.arg("action");
  if (action == "pulse") {
    openGate(entryServo, entryGateOpen, entryStartTime, true);
    server.send(200, "text/plain", "Entry gate pulsed (auto-closing in 3s)");
  } else if (action == "open") {
    openGate(entryServo, entryGateOpen, entryStartTime, false);
    server.send(200, "text/plain", "Entry gate held OPEN");
  } else if (action == "close") {
    closeGate(entryServo, entryGateOpen, "ENTRY");
    server.send(200, "text/plain", "Entry gate CLOSED");
  } else {
    server.send(400, "text/plain", "Invalid action: use pulse, open, or close");
  }
}

// Gate API: /api/gate/exit?action=pulse|open|close
void handleGateExit() {
  sendCORS();
  String action = server.arg("action");
  if (action == "pulse") {
    openGate(exitServo, exitGateOpen, exitStartTime, true);
    server.send(200, "text/plain", "Exit gate pulsed (auto-closing in 3s)");
  } else if (action == "open") {
    openGate(exitServo, exitGateOpen, exitStartTime, false);
    server.send(200, "text/plain", "Exit gate held OPEN");
  } else if (action == "close") {
    closeGate(exitServo, exitGateOpen, "EXIT");
    server.send(200, "text/plain", "Exit gate CLOSED");
  } else {
    server.send(400, "text/plain", "Invalid action: use pulse, open, or close");
  }
}

// Slots API: /api/slots?action=inc|dec|reset
void handleSlots() {
  sendCORS();
  String action = server.arg("action");
  if (action == "inc") {
    if (available_slots > 0) {
      occupied_slots++;
      available_slots--;
      saveSlotCount();
      if (!messageActive) updateLCD();
      server.send(200, "text/plain", "Slot count incremented (+1)");
    } else {
      server.send(400, "text/plain", "Cannot increment: Parking Full");
    }
  } else if (action == "dec") {
    if (occupied_slots > 0) {
      occupied_slots--;
      available_slots++;
      saveSlotCount();
      if (!messageActive) updateLCD();
      server.send(200, "text/plain", "Slot count decremented (-1)");
    } else {
      server.send(400, "text/plain", "Cannot decrement: Parking Empty");
    }
  } else if (action == "reset") {
    occupied_slots = 0;
    available_slots = TOTAL_SLOTS;
    saveSlotCount();
    if (!messageActive) updateLCD();
    server.send(200, "text/plain", "Slots reset to 0 occupied");
  } else {
    server.send(400, "text/plain", "Invalid action: use inc, dec, or reset");
  }
}

void handleReset() {
  sendCORS();
  occupied_slots = 0;
  available_slots = TOTAL_SLOTS;
  saveSlotCount();
  if (!messageActive) updateLCD();
  server.send(200, "text/plain", "Parking slots reset to 0 occupied.");
}

void setupWebServer() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  // Serve static files from LittleFS
  server.on("/", HTTP_GET, []() { handleFileRead("/index.html", "text/html"); });
  server.on("/index.html", HTTP_GET, []() { handleFileRead("/index.html", "text/html"); });
  server.on("/style.css", HTTP_GET, []() { handleFileRead("/style.css", "text/css"); });
  server.on("/app.js", HTTP_GET, []() { handleFileRead("/app.js", "application/javascript"); });

  // Telemetry and REST control routes
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/api/gate/entry", HTTP_GET, handleGateEntry);
  server.on("/api/gate/exit", HTTP_GET, handleGateExit);
  server.on("/api/slots", HTTP_GET, handleSlots);
  server.on("/reset", HTTP_GET, handleReset);

  server.begin();
}

// =============================
// SETUP
// =============================
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);

  // 1. Initialize LittleFS Flash Filesystem
  setupLittleFS();

  // 2. Start Web Server and Wi-Fi SoftAP
  setupWebServer();

  // 3. Configure GPIO 25 & 26
  gpio_reset_pin((gpio_num_t)ENTRY_IR_PIN);
  gpio_reset_pin((gpio_num_t)EXIT_IR_PIN);
  pinMode(ENTRY_IR_PIN, INPUT);
  pinMode(EXIT_IR_PIN, INPUT);

  // 4. Allocate Servo PWM Timers
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);

  entryServo.setPeriodHertz(50);
  entryServo.attach(ENTRY_SERVO_PIN, 500, 2400);

  exitServo.setPeriodHertz(50);
  exitServo.attach(EXIT_SERVO_PIN, 500, 2400);

  entryServo.write(CLOSED_ANGLE);
  exitServo.write(CLOSED_ANGLE);

  // 5. Restore Persistent Slot Counts
  prefs.begin("parking", false);
  loadSlotCount();

  // 6. Initialize I2C LCD Display
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Smart Parking");
  lcd.setCursor(0, 1);
  lcd.print("Web Control Ready");
  delay(1500);

  lcd.clear();
  updateLCD();

  Serial.println();
  Serial.println("=========================================");
  Serial.println("SMART PARKING - WEB CONTROL SYSTEM READY");
  Serial.println("=========================================");
  Serial.print("Access Point: "); Serial.println(AP_SSID);
  Serial.println("Web Dashboard URL: http://192.168.4.1");
}

// =============================
// LOOP
// =============================
void loop() {
  server.handleClient();
  handleTempMessage();

  // ==================================================
  // AUTOMATIC ENTRY SENSOR TRIGGER
  // ==================================================
  if (debouncedFallingEdge(entryIR)) {
    Serial.println(">>> SENSOR: ENTRY DETECTED");

    if (available_slots > 0) {
      occupied_slots++;
      available_slots--;
      saveSlotCount();

      if (!messageActive) updateLCD();

      openGate(entryServo, entryGateOpen, entryStartTime, true);
      Serial.println(">>> ENTRY SERVO OPEN");
    } else {
      Serial.println(">>> PARKING FULL");
      showTempMessage("PARKING FULL!", "PLEASE WAIT");
    }
  }

  closeGateIfDue(entryServo, entryGateOpen, entryStartTime, entryAutoClose, "ENTRY");

  // ==================================================
  // AUTOMATIC EXIT SENSOR TRIGGER
  // ==================================================
  if (debouncedFallingEdge(exitIR)) {
    Serial.println(">>> SENSOR: EXIT DETECTED");

    if (occupied_slots > 0) {
      occupied_slots--;
      available_slots++;
      saveSlotCount();

      if (!messageActive) updateLCD();

      openGate(exitServo, exitGateOpen, exitStartTime, true);
      Serial.println(">>> EXIT SERVO OPEN");
    } else {
      Serial.println(">>> PARKING EMPTY");
      showTempMessage("PARKING EMPTY", "NO VEHICLES");
    }
  }

  closeGateIfDue(exitServo, exitGateOpen, exitStartTime, exitAutoClose, "EXIT");

  delay(10);
}
