#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>

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
// PERSISTENT STORAGE (survives reboot/power loss)
// =============================
Preferences prefs;

// =============================
// WIRING
// =============================
// ENTRY: IR -> GPIO 26 | Servo -> GPIO 14
// EXIT:  IR -> GPIO 25 | Servo -> GPIO 13

#define ENTRY_IR_PIN     26
#define EXIT_IR_PIN      25

#define ENTRY_SERVO_PIN  14
#define EXIT_SERVO_PIN   13

// =============================
// PARKING
// =============================
const int TOTAL_SLOTS = 20;

int occupied_slots = 0;
int available_slots = TOTAL_SLOTS;

// =============================
// SERVO POSITIONS
// =============================
const int CLOSED_ANGLE = 0;
const int OPEN_ANGLE   = 90;

// =============================
// GATE TIMING
// =============================
const unsigned long GATE_TIME = 3000;   // how long gate stays open

unsigned long entryStartTime = 0;
unsigned long exitStartTime  = 0;

bool entryGateOpen = false;
bool exitGateOpen  = false;

// =============================
// IR SENSOR DEBOUNCE STATE (100% ORIGINAL)
// =============================
struct IRSensor {
  int pin;
  int stableState;       // debounced state currently accepted
  int lastRawState;      // last raw reading (for edge detection)
  unsigned long lastChangeTime;
  bool processed;        // prevents re-triggering while held LOW
};

const unsigned long DEBOUNCE_TIME = 50; // ms - ignore flicker shorter than this

IRSensor entryIR = { ENTRY_IR_PIN, HIGH, HIGH, 0, false };
IRSensor exitIR  = { EXIT_IR_PIN,  HIGH, HIGH, 0, false };

// =============================
// TEMPORARY LCD MESSAGE (non-blocking)
// =============================
bool messageActive = false;
unsigned long messageStartTime = 0;
const unsigned long MESSAGE_TIME = 1500; // ms

// =============================
// LCD CHANGE TRACKING (avoid redundant I2C writes)
// =============================
int lastPrintedOccupied  = -1;
int lastPrintedAvailable = -1;

// =============================
// WEB DASHBOARD HTML
// =============================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Smart Parking Live Status</title>
  <style>
    :root {
      --bg: #0f172a;
      --card: #1e293b;
      --text: #f8fafc;
      --muted: #94a3b8;
      --green: #22c55e;
      --red: #ef4444;
      --amber: #f59e0b;
      --blue: #38bdf8;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: system-ui, -apple-system, sans-serif; }
    body { background: var(--bg); color: var(--text); padding: 20px; display: flex; flex-direction: column; align-items: center; min-height: 100vh; }
    .container { width: 100%; max-width: 420px; }
    header { text-align: center; margin-bottom: 20px; }
    header h1 { font-size: 1.5rem; font-weight: 700; margin-bottom: 6px; }
    .live-pill { display: inline-flex; align-items: center; gap: 6px; background: rgba(34,197,94,0.15); color: var(--green); padding: 4px 12px; border-radius: 999px; font-size: 0.8rem; font-weight: 600; }
    .dot { width: 8px; height: 8px; background: var(--green); border-radius: 50%; animation: pulse 1.5s infinite; }
    @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.3; } }
    
    .status-banner { text-align: center; padding: 14px; border-radius: 12px; font-weight: 700; font-size: 1.1rem; margin-bottom: 16px; transition: 0.3s; }
    .banner-ok { background: rgba(34,197,94,0.2); color: var(--green); border: 1px solid var(--green); }
    .banner-full { background: rgba(239,68,68,0.2); color: var(--red); border: 1px solid var(--red); }

    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-bottom: 16px; }
    .card { background: var(--card); padding: 18px; border-radius: 14px; text-align: center; box-shadow: 0 4px 12px rgba(0,0,0,0.25); }
    .card .label { font-size: 0.85rem; color: var(--muted); text-transform: uppercase; letter-spacing: 0.5px; }
    .card .value { font-size: 2.6rem; font-weight: 800; margin-top: 6px; }
    .val-avail { color: var(--green); }
    .val-occ { color: var(--amber); }

    .progress-card { background: var(--card); padding: 16px; border-radius: 14px; margin-bottom: 16px; }
    .progress-header { display: flex; justify-content: space-between; font-size: 0.85rem; color: var(--muted); margin-bottom: 8px; }
    .progress-track { width: 100%; height: 12px; background: #334155; border-radius: 999px; overflow: hidden; }
    .progress-fill { height: 100%; background: var(--blue); width: 0%; transition: width 0.4s ease; }

    .gates-card { background: var(--card); padding: 16px; border-radius: 14px; display: flex; justify-content: space-around; }
    .gate-item { text-align: center; }
    .gate-item span { display: block; font-size: 0.8rem; color: var(--muted); margin-bottom: 6px; }
    .badge { padding: 4px 10px; border-radius: 6px; font-size: 0.8rem; font-weight: 700; }
    .badge-open { background: rgba(34,197,94,0.2); color: var(--green); }
    .badge-closed { background: #334155; color: var(--muted); }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <h1>Smart Parking Hub</h1>
      <div class="live-pill"><span class="dot"></span> LIVE UPDATES</div>
    </header>

    <div id="banner" class="status-banner banner-ok">CHECKING SLOTS...</div>

    <div class="grid">
      <div class="card">
        <div class="label">Available</div>
        <div id="avail" class="value val-avail">--</div>
      </div>
      <div class="card">
        <div class="label">Occupied</div>
        <div id="occ" class="value val-occ">--</div>
      </div>
    </div>

    <div class="progress-card">
      <div class="progress-header">
        <span>Occupancy Rate</span>
        <span id="cap-text">0 / 20 Slots</span>
      </div>
      <div class="progress-track">
        <div id="bar" class="progress-fill"></div>
      </div>
    </div>

    <div class="gates-card">
      <div class="gate-item">
        <span>Entry Gate</span>
        <div id="entry-gate" class="badge badge-closed">CLOSED</div>
      </div>
      <div class="gate-item">
        <span>Exit Gate</span>
        <div id="exit-gate" class="badge badge-closed">CLOSED</div>
      </div>
    </div>
  </div>

  <script>
    async function fetchStatus() {
      try {
        const res = await fetch('/status');
        const data = await res.json();

        document.getElementById('avail').textContent = data.available;
        document.getElementById('occ').textContent = data.occupied;
        document.getElementById('cap-text').textContent = `${data.occupied} / ${data.total} Slots`;

        const pct = Math.round((data.occupied / data.total) * 100);
        document.getElementById('bar').style.width = pct + '%';

        const banner = document.getElementById('banner');
        if (data.available > 0) {
          banner.textContent = `SLOTS AVAILABLE — PROCEED TO ENTRY`;
          banner.className = 'status-banner banner-ok';
        } else {
          banner.textContent = `PARKING FULL — PLEASE WAIT`;
          banner.className = 'status-banner banner-full';
        }

        const entryEl = document.getElementById('entry-gate');
        entryEl.textContent = data.entryOpen ? 'OPEN' : 'CLOSED';
        entryEl.className = 'badge ' + (data.entryOpen ? 'badge-open' : 'badge-closed');

        const exitEl = document.getElementById('exit-gate');
        exitEl.textContent = data.exitOpen ? 'OPEN' : 'CLOSED';
        exitEl.className = 'badge ' + (data.exitOpen ? 'badge-open' : 'badge-closed');
      } catch (e) {
        console.error('Connection lost', e);
      }
    }

    setInterval(fetchStatus, 1000);
    fetchStatus();
  </script>
</body>
</html>
)rawliteral";

// =============================
// DEBOUNCED READ (100% ORIGINAL)
// Returns true exactly once when a clean LOW edge is confirmed
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
// LCD (100% ORIGINAL)
// =============================
void updateLCD() {
  if (occupied_slots == lastPrintedOccupied &&
      available_slots == lastPrintedAvailable) {
    return; // nothing changed, skip I2C write
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

  // force LCD refresh once message clears
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
// SLOT COUNT PERSISTENCE
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
// GATE CONTROL
// =============================
void openGate(Servo &servo, bool &gateOpenFlag, unsigned long &startTime) {
  servo.write(OPEN_ANGLE);
  gateOpenFlag = true;
  startTime = millis();
}

void closeGateIfDue(Servo &servo, bool &gateOpenFlag, unsigned long startTime, const char* label) {
  if (gateOpenFlag && (millis() - startTime >= GATE_TIME)) {
    servo.write(CLOSED_ANGLE);
    gateOpenFlag = false;
    Serial.print(">>> ");
    Serial.print(label);
    Serial.println(" SERVO CLOSED");
  }
}

// =============================
// WEB SERVER HANDLERS
// =============================
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  String json = "{";
  json += "\"total\":" + String(TOTAL_SLOTS) + ",";
  json += "\"occupied\":" + String(occupied_slots) + ",";
  json += "\"available\":" + String(available_slots) + ",";
  json += "\"entryOpen\":" + String(entryGateOpen ? "true" : "false") + ",";
  json += "\"exitOpen\":" + String(exitGateOpen ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleReset() {
  occupied_slots = 0;
  available_slots = TOTAL_SLOTS;
  saveSlotCount();
  if (!messageActive) updateLCD();
  server.send(200, "text/plain", "Parking slots reset to 0 occupied.");
}

void setupWebServer() {
  WiFi.persistent(false); // Stop Wi-Fi from wearing out flash or fighting Preferences
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/reset", HTTP_GET, handleReset);

  server.begin();
}

// =============================
// SETUP
// =============================
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);

  // 1. Start Wi-Fi FIRST before configuring GPIO 25 / 26
  setupWebServer();

  // 2. Detach GPIO 25 & 26 from RTC/DAC/ADC2 multiplexing and set as pure digital INPUT
  gpio_reset_pin((gpio_num_t)ENTRY_IR_PIN);
  gpio_reset_pin((gpio_num_t)EXIT_IR_PIN);
  pinMode(ENTRY_IR_PIN, INPUT);
  pinMode(EXIT_IR_PIN, INPUT);

  // 3. Allocate Servo PWM Timers
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);

  entryServo.setPeriodHertz(50);
  entryServo.attach(ENTRY_SERVO_PIN, 500, 2400);

  exitServo.setPeriodHertz(50);
  exitServo.attach(EXIT_SERVO_PIN, 500, 2400);

  entryServo.write(CLOSED_ANGLE);
  exitServo.write(CLOSED_ANGLE);

  prefs.begin("parking", false);
  loadSlotCount();

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Smart Parking");
  lcd.setCursor(0, 1);
  lcd.print("System Ready");
  delay(1500);

  lcd.clear();
  updateLCD();

  Serial.println();
  Serial.println("======================");
  Serial.println("SMART PARKING READY");
  Serial.println("======================");
  Serial.print("Restored occupied count: ");
  Serial.println(occupied_slots);
}

// =============================
// LOOP
// =============================
void loop() {
  server.handleClient();

  handleTempMessage();

  // ==================================================
  // ENTRY
  // ==================================================
  if (debouncedFallingEdge(entryIR)) {
    Serial.println(">>> ENTRY DETECTED");

    if (available_slots > 0) {
      occupied_slots++;
      available_slots--;
      saveSlotCount();

      if (!messageActive) updateLCD();

      openGate(entryServo, entryGateOpen, entryStartTime);
      Serial.println(">>> ENTRY SERVO OPEN");
    } else {
      Serial.println(">>> PARKING FULL");
      showTempMessage("PARKING FULL!", "PLEASE WAIT");
    }
  }

  closeGateIfDue(entryServo, entryGateOpen, entryStartTime, "ENTRY");

  // ==================================================
  // EXIT
  // ==================================================
  if (debouncedFallingEdge(exitIR)) {
    Serial.println(">>> EXIT DETECTED");

    if (occupied_slots > 0) {
      occupied_slots--;
      available_slots++;
      saveSlotCount();

      if (!messageActive) updateLCD();

      openGate(exitServo, exitGateOpen, exitStartTime);
      Serial.println(">>> EXIT SERVO OPEN");
    } else {
      Serial.println(">>> PARKING EMPTY");
      showTempMessage("PARKING EMPTY", "NO VEHICLES");
    }
  }

  closeGateIfDue(exitServo, exitGateOpen, exitStartTime, "EXIT");

  // Small yield, no long blocking delay
  delay(10);
}