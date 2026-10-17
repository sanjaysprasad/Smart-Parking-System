#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// Required for brownout bypass and clean GPIO pad reset
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/gpio.h"

// ========================================================
// 1. WI-FI & FIREBASE CONFIGURATION
// ========================================================
// Enter your Home / Office / Mobile Hotspot Wi-Fi details:
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// Firebase Realtime Database Host
const char* FIREBASE_HOST = "https://smart-parking-system-5d991-default-rtdb.firebaseio.com";

// ========================================================
// 2. HARDWARE PIN DEFINITIONS
// ========================================================
#define ENTRY_IR_PIN     26
#define EXIT_IR_PIN      25

#define ENTRY_SERVO_PIN  14
#define EXIT_SERVO_PIN   13

// ========================================================
// 3. LCD & PERIPHERALS
// ========================================================
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo entryServo;
Servo exitServo;
Preferences prefs;

// ========================================================
// 4. PARKING CONFIGURATION
// ========================================================
const int TOTAL_SLOTS = 20;
int occupied_slots = 0;
int available_slots = TOTAL_SLOTS;

const int CLOSED_ANGLE = 0;
const int OPEN_ANGLE   = 90;
const unsigned long GATE_TIME = 3000;   // 3 seconds auto-close

unsigned long entryStartTime = 0;
unsigned long exitStartTime  = 0;

bool entryGateOpen  = false;
bool exitGateOpen   = false;
bool entryAutoClose = true;
bool exitAutoClose  = true;

// ========================================================
// 5. IR DEBOUNCING
// ========================================================
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

// ========================================================
// 6. TIMING & CLOUD POLL INTERVALS
// ========================================================
unsigned long lastCloudPollTime = 0;
const unsigned long CLOUD_POLL_INTERVAL = 750; // Check for remote commands every 750ms

bool messageActive = false;
unsigned long messageStartTime = 0;
const unsigned long MESSAGE_TIME = 1500;

int lastPrintedOccupied  = -1;
int lastPrintedAvailable = -1;

// ========================================================
// 7. DEBOUNCED READ
// ========================================================
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

// ========================================================
// 8. LCD DISPLAY FUNCTIONS
// ========================================================
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

// ========================================================
// 9. GATE CONTROL
// ========================================================
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
    // Sync closed state to cloud
    syncStateToFirebase();
  }
}

// ========================================================
// 10. NVS FLASH PERSISTENCE
// ========================================================
void saveSlotCount() {
  prefs.putInt("occupied", occupied_slots);
}

void loadSlotCount() {
  occupied_slots = prefs.getInt("occupied", 0);
  if (occupied_slots < 0) occupied_slots = 0;
  if (occupied_slots > TOTAL_SLOTS) occupied_slots = TOTAL_SLOTS;
  available_slots = TOTAL_SLOTS - occupied_slots;
}

// ========================================================
// 11. FIREBASE REALTIME CLOUD INTEGRATION (HTTPS REST)
// ========================================================
void syncStateToFirebase() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure(); // Cloud SSL without certificate expiration issues

  HTTPClient https;
  String url = String(FIREBASE_HOST) + "/parking.json";

  if (https.begin(client, url)) {
    https.addHeader("Content-Type", "application/json");

    String json = "{";
    json += "\"total\":" + String(TOTAL_SLOTS) + ",";
    json += "\"occupied\":" + String(occupied_slots) + ",";
    json += "\"available\":" + String(available_slots) + ",";
    json += "\"entryGate\":\"" + String(entryGateOpen ? "OPEN" : "CLOSED") + "\",";
    json += "\"exitGate\":\"" + String(exitGateOpen ? "OPEN" : "CLOSED") + "\"";
    json += "}";

    int httpCode = https.sendRequest("PATCH", json);
    if (httpCode > 0) {
      Serial.print("[Cloud] State synced. Code: ");
      Serial.println(httpCode);
    }
    https.end();
  }
}

void checkCloudCommands() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastCloudPollTime < CLOUD_POLL_INTERVAL) return;
  lastCloudPollTime = millis();

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String url = String(FIREBASE_HOST) + "/commands.json";

  if (https.begin(client, url)) {
    int httpCode = https.GET();
    if (httpCode == HTTP_CODE_OK) {
      String payload = https.getString();

      // Check Entry Gate Command
      if (payload.indexOf("\"entry\":\"pulse\"") >= 0) {
        Serial.println("[Cloud] Remote pulse ENTRY gate");
        openGate(entryServo, entryGateOpen, entryStartTime, true);
        clearCloudCommand("entry");
        syncStateToFirebase();
      } else if (payload.indexOf("\"entry\":\"open\"") >= 0) {
        Serial.println("[Cloud] Remote hold open ENTRY gate");
        openGate(entryServo, entryGateOpen, entryStartTime, false);
        clearCloudCommand("entry");
        syncStateToFirebase();
      } else if (payload.indexOf("\"entry\":\"close\"") >= 0) {
        Serial.println("[Cloud] Remote close ENTRY gate");
        closeGate(entryServo, entryGateOpen, "ENTRY");
        clearCloudCommand("entry");
        syncStateToFirebase();
      }

      // Check Exit Gate Command
      if (payload.indexOf("\"exit\":\"pulse\"") >= 0) {
        Serial.println("[Cloud] Remote pulse EXIT gate");
        openGate(exitServo, exitGateOpen, exitStartTime, true);
        clearCloudCommand("exit");
        syncStateToFirebase();
      } else if (payload.indexOf("\"exit\":\"open\"") >= 0) {
        Serial.println("[Cloud] Remote hold open EXIT gate");
        openGate(exitServo, exitGateOpen, exitStartTime, false);
        clearCloudCommand("exit");
        syncStateToFirebase();
      } else if (payload.indexOf("\"exit\":\"close\"") >= 0) {
        Serial.println("[Cloud] Remote close EXIT gate");
        closeGate(exitServo, exitGateOpen, "EXIT");
        clearCloudCommand("exit");
        syncStateToFirebase();
      }

      // Check Reset Command
      if (payload.indexOf("\"reset\":true") >= 0) {
        Serial.println("[Cloud] Remote RESET slots to 0");
        occupied_slots = 0;
        available_slots = TOTAL_SLOTS;
        saveSlotCount();
        if (!messageActive) updateLCD();
        clearCloudCommand("reset");
        syncStateToFirebase();
      }

      // Check Slot Adjustment Commands
      if (payload.indexOf("\"slots\":\"inc\"") >= 0) {
        if (available_slots > 0) {
          occupied_slots++;
          available_slots--;
          saveSlotCount();
          if (!messageActive) updateLCD();
          syncStateToFirebase();
        }
        clearCloudCommand("slots");
      } else if (payload.indexOf("\"slots\":\"dec\"") >= 0) {
        if (occupied_slots > 0) {
          occupied_slots--;
          available_slots++;
          saveSlotCount();
          if (!messageActive) updateLCD();
          syncStateToFirebase();
        }
        clearCloudCommand("slots");
      }
    }
    https.end();
  }
}

void clearCloudCommand(const char* cmdKey) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String url = String(FIREBASE_HOST) + "/commands/" + String(cmdKey) + ".json";

  if (https.begin(client, url)) {
    https.addHeader("Content-Type", "application/json");
    if (strcmp(cmdKey, "reset") == 0) {
      https.PUT("false");
    } else {
      https.PUT("\"idle\"");
    }
    https.end();
  }
}

// ========================================================
// 12. SETUP
// ========================================================
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Bypass brownout
  Serial.begin(115200);

  // Configure GPIO 25 & 26
  gpio_reset_pin((gpio_num_t)ENTRY_IR_PIN);
  gpio_reset_pin((gpio_num_t)EXIT_IR_PIN);
  pinMode(ENTRY_IR_PIN, INPUT);
  pinMode(EXIT_IR_PIN, INPUT);

  // Allocate Servo PWM Timers
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);

  entryServo.setPeriodHertz(50);
  entryServo.attach(ENTRY_SERVO_PIN, 500, 2400);

  exitServo.setPeriodHertz(50);
  exitServo.attach(EXIT_SERVO_PIN, 500, 2400);

  entryServo.write(CLOSED_ANGLE);
  exitServo.write(CLOSED_ANGLE);

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Smart Parking");
  lcd.setCursor(0, 1);
  lcd.print("Connecting WiFi");

  // Restore stored slot count
  prefs.begin("parking", false);
  loadSlotCount();

  // Connect to Wi-Fi
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 25) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  lcd.clear();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[Wi-Fi] Connected successfully!");
    Serial.print("[Wi-Fi] IP Address: ");
    Serial.println(WiFi.localIP());

    lcd.setCursor(0, 0);
    lcd.print("Cloud Connected!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(1500);

    // Initial sync with Firebase
    syncStateToFirebase();
  } else {
    Serial.println("\n[Wi-Fi] Warning: Offline Mode. Operates locally.");
    lcd.setCursor(0, 0);
    lcd.print("WiFi Offline");
    lcd.setCursor(0, 1);
    lcd.print("Local Mode Only");
    delay(1500);
  }

  lcd.clear();
  updateLCD();
}

// ========================================================
// 13. MAIN LOOP
// ========================================================
void loop() {
  handleTempMessage();

  // Check and process cloud commands from web app
  checkCloudCommands();

  // ==================================================
  // AUTOMATIC ENTRY SENSOR TRIGGER
  // ==================================================
  if (debouncedFallingEdge(entryIR)) {
    Serial.println(">>> HARDWARE: ENTRY SENSOR TRIGGERED");

    if (available_slots > 0) {
      occupied_slots++;
      available_slots--;
      saveSlotCount();

      if (!messageActive) updateLCD();

      openGate(entryServo, entryGateOpen, entryStartTime, true);
      Serial.println(">>> ENTRY SERVO OPENED");

      // Push real-time update to Firebase
      syncStateToFirebase();
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
    Serial.println(">>> HARDWARE: EXIT SENSOR TRIGGERED");

    if (occupied_slots > 0) {
      occupied_slots--;
      available_slots++;
      saveSlotCount();

      if (!messageActive) updateLCD();

      openGate(exitServo, exitGateOpen, exitStartTime, true);
      Serial.println(">>> EXIT SERVO OPENED");

      // Push real-time update to Firebase
      syncStateToFirebase();
    } else {
      Serial.println(">>> PARKING EMPTY");
      showTempMessage("PARKING EMPTY", "NO VEHICLES");
    }
  }

  closeGateIfDue(exitServo, exitGateOpen, exitStartTime, exitAutoClose, "EXIT");

  delay(10);
}
