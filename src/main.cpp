// ======================================================================
// PET FEEDER — Arduino Uno / Nano + DS1302 + L298N
// ======================================================================
// Required libraries (PlatformIO → platformio.ini):
//   makuna/RTC@^2.5.0
// Arduino IDE → install via Library Manager:
//   RTC by Makuna  (supports DS1302, DS1307, DS3231, DS3234, PCF8563)
// ======================================================================

#include <RtcDS1302.h>

// ==================== DS1302 PINS ====================
// VCC → 5V,  GND → GND  (hardware power)
#define DS1302_DAT   11   // DAT (I/O)
#define DS1302_CLK   10   // CLK (SCLK)
#define DS1302_RST   12   // RST (CE)

// ==================== OTHER PINS ====================
#define POT_PIN       A0   // 100kΩ potentiometer → portion size selection
#define TEST_BTN_PIN  2    // TEST button → unscheduled run (INPUT_PULLUP)
#define IN1_PIN       6    // L298N IN1 (direction)
#define IN2_PIN       7    // L298N IN2 (direction)
// ENA on L298N not connected — ENA jumper left in place (always HIGH)

// ==================== MOTOR CONSTANTS ====================
#define MIN_DURATION  500  // Minimum dispensing duration, ms  (= 0.5 sec)
#define MAX_DURATION  1000 // Maximum dispensing duration, ms (= 1 sec)

// ==================== FEEDING SCHEDULE ====================
// Format: { hours (0–23), minutes (0–59) }
// Add/remove/change entries as needed.
const uint8_t FEEDING_TIMES[][2] = {
  { 9,  0},   // 09:00
  {19,  0}    // 19:00
};
const uint8_t FEEDING_COUNT = sizeof(FEEDING_TIMES) / sizeof(FEEDING_TIMES[0]);

// ==================== DEBUG SETTINGS ====================
#define SERIAL_BAUD   9600   // Serial Monitor baud rate
#define SERIAL_PERIOD 1000   // Serial output period, ms

// ==================== BUTTON DEBOUNCE ====================
#define DEBOUNCE_MS   50     // Button stabilization time, ms

// ==================== GLOBAL VARIABLES ====================
// DS1302 hardware interface: DAT, CLK, RST
ThreeWire                   dsWire(DS1302_DAT, DS1302_CLK, DS1302_RST);
RtcDS1302<ThreeWire>        rtc(dsWire);

// ---- Current time (updated every loop cycle) ----
uint8_t currentHour   = 0;
uint8_t currentMinute = 0;
uint8_t currentSecond = 0;

// ---- Motor state (non-blocking state machine) ----
enum MotorState { IDLE, RUNNING };
MotorState motorState  = IDLE;      // Current state
unsigned long motorStartTime = 0;   // Motor start time (millis)
unsigned long motorDuration  = 0;   // Current dispensing duration, ms

// ---- Protection against re-triggering in the same minute ----
int lastFedMinuteOfDay = -1;        // Minute of day of last feeding START
                                    // (hour * 60 + minute). -1 = not fed yet.

// ---- TEST button debounce ----
bool     lastBtnReading   = HIGH;   // Previous raw pin reading
bool     btnPressHandled  = false;  // Flag: press already handled (until released)
unsigned long btnStableTime = 0;    // Time of last button signal change

// ---- Periodic Serial output ----
unsigned long lastSerialTime  = 0;

// ---- Current values (for Serial output) ----
int          currentPotValue    = 0;
unsigned long currentDuration   = 0; // Calculated duration from potentiometer position


// ==================================================================
//  HELPER FUNCTIONS
// ==================================================================

// ---------------------------------------------------------------
// Formatted time output to Serial (HH:MM:SS with leading zeros)
// ---------------------------------------------------------------
void printTime(uint8_t h, uint8_t m, uint8_t s) {
  if (h < 10) Serial.print('0');
  Serial.print(h);
  Serial.print(':');
  if (m < 10) Serial.print('0');
  Serial.print(m);
  Serial.print(':');
  if (s < 10) Serial.print('0');
  Serial.print(s);
}

// ---------------------------------------------------------------
// Start motor for a given time (ms)
//   — energizes coils in one direction
//   — records start time for non-blocking stop
// ---------------------------------------------------------------
void startMotor(unsigned long durationMs) {
  digitalWrite(IN1_PIN, HIGH);       // Rotate forward
  digitalWrite(IN2_PIN, LOW);
  motorStartTime = millis();
  motorDuration  = durationMs;
  motorState     = RUNNING;
}

// ---------------------------------------------------------------
// Full motor stop
// ---------------------------------------------------------------
void stopMotor() {
  digitalWrite(IN1_PIN, LOW);        // Remove power from coils
  digitalWrite(IN2_PIN, LOW);
  motorState = IDLE;
}

// ---------------------------------------------------------------
// Check if current time matches a schedule entry
// ---------------------------------------------------------------
bool isFeedingTime(uint8_t hour, uint8_t minute) {
  for (uint8_t i = 0; i < FEEDING_COUNT; i++) {
    if (hour   == FEEDING_TIMES[i][0] &&
        minute == FEEDING_TIMES[i][1]) {
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------
// Trigger feeding cycle (single entry point for schedule and TEST)
//   reason — reason string (F("SCHEDULE") / F("TEST"))
// ---------------------------------------------------------------
void triggerFeeding(const __FlashStringHelper *reason) {
  // Read potentiometer and calculate dispensing duration
  int potValue = 1023 - analogRead(POT_PIN);
  unsigned long duration = map(potValue, 0, 1023, MIN_DURATION, MAX_DURATION);

  startMotor(duration);

  // Prevent re-triggering in the same minute
  int minuteOfDay = currentHour * 60 + currentMinute;
  lastFedMinuteOfDay = minuteOfDay;

  // --- Serial output ---
  Serial.print(F("[FEEDING] Reason: "));
  Serial.print(reason);
  Serial.print(F("  |  "));
  printTime(currentHour, currentMinute, currentSecond);
  Serial.print(F("  |  Potentiometer: "));
  Serial.print(potValue);
  Serial.print(F(" ("));
  Serial.print(map(potValue, 0, 1023, 0, 100));   // 0–100%
  Serial.print(F("%)  |  Duration: "));
  Serial.print(duration / 1000.0, 1);
  Serial.println(F(" sec"));
}


// ==================================================================
//  SETUP
// ==================================================================
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(100);   // Pause to let Serial Monitor open

  // ---------- DS1302 Initialization ----------
  rtc.Begin();

  // Check DS1302 data validity
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  if (!rtc.IsDateTimeValid()) {
    // Data invalid (e.g. module lost power)
    Serial.println(F("[RTC] Invalid time. Set to compile time."));
    rtc.SetDateTime(compiled);
  }

  // To forcibly set exact time — uncomment:
  // rtc.SetDateTime(RtcDateTime(2026, 6, 22, 8, 30, 0));
  //  (year, month, day, hour, minute, second)

  // ---------- Pin Setup ----------
  pinMode(TEST_BTN_PIN, INPUT_PULLUP); // Button: pull-up to HIGH, press = LOW
  pinMode(IN1_PIN,       OUTPUT);
  pinMode(IN2_PIN,       OUTPUT);

  // ---------- Initial State ----------
  stopMotor();

  // ---------- Greeting ----------
  Serial.println(F("\n========================================"));
  Serial.println(F("     PET FEEDER v1.0"));
  Serial.println(F("========================================"));
  Serial.print(F("RTC: DS1302 (DAT=D"));
  Serial.print(DS1302_DAT);
  Serial.print(F(", CLK=D"));
  Serial.print(DS1302_CLK);
  Serial.print(F(", RST=D"));
  Serial.print(DS1302_RST);
  Serial.println(F(")"));
  Serial.println(F("Feeding schedule:"));
  for (uint8_t i = 0; i < FEEDING_COUNT; i++) {
    Serial.print(F("  * "));
    if (FEEDING_TIMES[i][0] < 10) Serial.print('0');
    Serial.print(FEEDING_TIMES[i][0]);
    Serial.print(':');
    if (FEEDING_TIMES[i][1] < 10) Serial.print('0');
    Serial.println(FEEDING_TIMES[i][1]);
  }
  Serial.println(F("----------------------------------------"));
}


// ==================================================================
//  LOOP  (non-blocking — no delay() for main logic)
// ==================================================================
void loop() {
  // ---------- Read time from DS1302 ----------
  RtcDateTime now = rtc.GetDateTime();

  // If data is corrupted on read — skip this iteration
  if (!now.IsValid()) {
    return;
  }

  currentHour   = now.Hour();
  currentMinute = now.Minute();
  currentSecond = now.Second();

  // ==================== 1. MOTOR HANDLING ====================
  // If motor is running — check if the set time has elapsed.
  // Use millis() to avoid blocking the rest of the code.
  if (motorState == RUNNING) {
    if (millis() - motorStartTime >= motorDuration) {
      stopMotor();
      Serial.print(F("[MOTOR] Stopped. Portion dispensed.  "));
      printTime(currentHour, currentMinute, currentSecond);
      Serial.println();
    }
  }

  // ==================== 2. POTENTIOMETER READING ====================
  currentPotValue  = 1023 - analogRead(POT_PIN);
  currentDuration  = map(currentPotValue, 0, 1023, MIN_DURATION, MAX_DURATION);

  // ==================== 3. TEST BUTTON (with debounce) ====================
  bool btnNow = digitalRead(TEST_BTN_PIN);

  // If signal changed — reset stabilization timer
  if (btnNow != lastBtnReading) {
    btnStableTime = millis();
  }
  lastBtnReading = btnNow;

  // If signal is stable longer than DEBOUNCE_MS — state is reliable
  if ((millis() - btnStableTime) >= DEBOUNCE_MS) {
    if (btnNow == LOW && !btnPressHandled && motorState == IDLE) {
      // Pressed (LOW due to INPUT_PULLUP), not yet handled, motor is idle
      btnPressHandled = true;
      Serial.println(F("[BUTTON] TEST button pressed"));
      triggerFeeding(F("TEST"));
    }
    if (btnNow == HIGH) {
      // Button released — reset flag, allow next press
      btnPressHandled = false;
    }
  }

  // ==================== 4. SCHEDULE CHECK ====================
  if (motorState == IDLE && isFeedingTime(currentHour, currentMinute)) {
    int minuteOfDay = currentHour * 60 + currentMinute;

    // Protection: skip if already fed this minute
    if (minuteOfDay != lastFedMinuteOfDay) {
      triggerFeeding(F("SCHEDULE"));
    }
  }

  // ==================== 5. PERIODIC SERIAL OUTPUT ====================
  if (millis() - lastSerialTime >= SERIAL_PERIOD) {
    lastSerialTime = millis();

    Serial.print('[');
    printTime(currentHour, currentMinute, currentSecond);
    Serial.print(F("]  Potentiometer: "));
    Serial.print(currentPotValue);
    Serial.print(F(" ("));
    Serial.print(map(currentPotValue, 0, 1023, 0, 100));
    Serial.print(F("%)  |  Portion: "));
    Serial.print(currentDuration / 1000.0, 1);
    Serial.print(F(" sec  |  Motor: "));
    Serial.println(motorState == RUNNING ? F("RUNNING") : F("STOPPED"));
  }
}
