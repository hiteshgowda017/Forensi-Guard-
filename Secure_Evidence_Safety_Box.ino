/*
 * Secure Evidence Safety Box
 * IoT / Embedded Security Project
 *
 * Features:
 * - PIN-based electronic access control
 * - Random PIN generation
 * - MPU6050 tamper detection
 * - BME680 environmental monitoring
 * - HC-05 Bluetooth communication
 * - GPS/location text logging through Bluetooth/Serial
 * - SH1106 OLED user interface
 * - 4x4 keypad navigation
 * - EEPROM-based persistent configuration
 *
 * Project Type: Academic / Educational IoT Prototype
 *
 * IMPORTANT:
 * Review and change the default PIN before deploying the hardware.
 * Do not expose real credentials in a public repository.
 */

#include <Wire.h>
#include <U8g2lib.h>
#include <Keypad.h>
#include <Adafruit_BME680.h>
#include <Adafruit_Sensor.h>
#include <SoftwareSerial.h>
#include <MPU6050.h>
#include <EEPROM.h>

// ---------- Display ----------
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ---------- Pins ----------
const int RELAY_PIN = 2;
const int BUZZER_PIN = A15; // change if not available

// ---------- Keypad ----------
const byte ROWS = 4;
const byte COLS = 4;
char hexaKeys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {10,9,8,7};
byte colPins[COLS] = {6,5,4,3};
Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

// ---------- Sensors & comms ----------
Adafruit_BME680 bme;
MPU6050 mpu;

// HC-05 SoftwareSerial (user-provided pins)
const uint8_t BT_TX_PIN = 14;
const uint8_t BT_RX_PIN = 15;
SoftwareSerial bluetooth(BT_RX_PIN, BT_TX_PIN); // rx, tx

// ---------- EEPROM layout ----------
const byte EEPROM_SIG_ADDR = 0;
const byte EEPROM_SIG_VAL = 0x42;
const int EEPROM_PIN_ADDR = 1;   // 4 bytes for pin
const int EEPROM_TEMP_ADDR = 10;
const int EEPROM_HUM_ADDR = 14;
const int EEPROM_GAS_ADDR = 18;

// ---------- System variables ----------
bool locked = true;
String pinCode = "8055";   // default (will be overwritten by EEPROM load)
String randomPin = "";
bool tamperDetected = false;
bool buzzerActive = false;
float tempThreshold = 35.0;
float humThreshold = 80.0;
float gasThreshold = 50.0;
unsigned long buzzerStartTime = 0;
unsigned long lastBTUpdate = 0;
bool mpuCalibrated = false;
String lastGPS = "N/A";
bool gpsFetching = false;
unsigned long gpsFetchStart = 0;

// ---------- BME globals (single-read values) ----------
float lastTemp = 0.0;
float lastHum = 0.0;
float lastGas = 0.0; // in KOhm
bool bmeOK = false;

enum MenuState {
  MAIN_MENU,
  STATUS_MENU,
  STATUS_DEVICE_LOCK,
  STATUS_SECURITY_MENU,
  STATUS_SECURITY_SHOWMASK,
  STATUS_SECURITY_CHANGE_OLD,   // enter old PIN
  STATUS_SECURITY_CHANGE_NEW,   // enter new PIN
  STATUS_GPS_MODE,
  STATUS_SENSOR_THRESHOLD_MENU,
  THRESHOLD_TEMP,
  THRESHOLD_HUM,
  THRESHOLD_GAS,
  SENSOR_STATUS,
  UNLOCK_MENU,
  ENTER_PIN,
  RANDOM_MENU,
  RANDOM_GENERATE,
  RANDOM_ENTER,
  ABOUT_US,
  RESTARTING
};
MenuState currentMenu = MAIN_MENU;

String inputBuffer = "";
String changeOldBuffer = "";
String changeNewBuffer = "";

// ---------- EEPROM helpers ----------
void saveEEPROMAll() {
  EEPROM.update(EEPROM_SIG_ADDR, EEPROM_SIG_VAL);
  for (int i = 0; i < 4; ++i) {
    char c = (i < pinCode.length()) ? pinCode.charAt(i) : '0';
    EEPROM.update(EEPROM_PIN_ADDR + i, c);
  }
  EEPROM.put(EEPROM_TEMP_ADDR, tempThreshold);
  EEPROM.put(EEPROM_HUM_ADDR, humThreshold);
  EEPROM.put(EEPROM_GAS_ADDR, gasThreshold);
  Serial.println("EEPROM: Saved settings.");
  bluetooth.println("EEPROM: Saved settings.");
}

void writeDefaultsToEEPROM() {
  pinCode = "8055";
  tempThreshold = 35.0;
  humThreshold = 80.0;
  gasThreshold = 50.0;
  saveEEPROMAll();
  Serial.println("EEPROM: Defaults written (8055).");
  bluetooth.println("EEPROM: Defaults written (8055).");
}

void loadEEPROMAll() {
  byte sig = EEPROM.read(EEPROM_SIG_ADDR);
  if (sig != EEPROM_SIG_VAL) {
    Serial.println("EEPROM: No valid signature - writing defaults.");
    writeDefaultsToEEPROM();
    return;
  }

  pinCode = "";
  for (int i = 0; i < 4; ++i) {
    char v = (char)EEPROM.read(EEPROM_PIN_ADDR + i);
    if (v >= '0' && v <= '9') pinCode += v;
    else pinCode += '0';
  }
  pinCode.trim();

  float t,h,g;
  EEPROM.get(EEPROM_TEMP_ADDR, t);
  EEPROM.get(EEPROM_HUM_ADDR, h);
  EEPROM.get(EEPROM_GAS_ADDR, g);
  if (!isnan(t) && t > -40 && t < 100) tempThreshold = t;
  if (!isnan(h) && h >= 0 && h <= 100) humThreshold = h;
  if (!isnan(g) && g > 0 && g < 10000) gasThreshold = g;

  Serial.print("EEPROM: Loaded PIN='"); Serial.print(pinCode); Serial.print("' len="); Serial.println(pinCode.length());
  bluetooth.print("EEPROM: Loaded PIN=");
}

// ---------- Small UI helper (blocking) ----------
void showTemporaryMessage(const char* line1, const char* line2, unsigned long ms) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont12_mf);
  u8g2.drawStr(0, 20, line1);
  if (line2 && strlen(line2)) u8g2.drawStr(0, 40, line2);
  u8g2.sendBuffer();
  if (ms > 0) delay(ms);
}

// ---------- Splash ----------
void showSplash() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont12_mf);
  const char* line1 = "|| JAI SHREE GURUDEV ||";
  const char* line2 = "BGSCET";
  const char* line3 = "EVIDENCE BOX STARTED";
  uint16_t w1 = u8g2.getStrWidth(line1);
  uint16_t w2 = u8g2.getStrWidth(line2);
  uint16_t w3 = u8g2.getStrWidth(line3);
  int x1 = (128 - w1) / 2;
  int x2 = (128 - w2) / 2;
  int x3 = (128 - w3) / 2;
  u8g2.drawStr(x1, 18, line1);
  u8g2.drawStr(x2, 34, line2);
  u8g2.drawStr(x3, 50, line3);
  u8g2.sendBuffer();
  delay(2000);
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  bluetooth.begin(9600);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // locked
  digitalWrite(BUZZER_PIN, LOW);

  u8g2.begin();
  Wire.begin();

  tone(BUZZER_PIN, 2000, 200);
  delay(200);
  noTone(BUZZER_PIN);

  if (!bme.begin()) {
    errorDisplay("BME680 Fail");
    while (1);
  }
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150);

  mpu.initialize();
  calibrateMPU();

  loadEEPROMAll();
  showSplash();
  sendBTSystemStatus();
}

// ---------- MPU calibration ----------
void calibrateMPU() {
  Serial.println("Calibrating MPU6050...");
  long ax=0, ay=0, az=0;
  for (int i=0;i<100;i++){
    int16_t x,y,z;
    mpu.getAcceleration(&x,&y,&z);
    ax += x; ay += y; az += z;
    delay(10);
  }
  mpu.setXAccelOffset(-(ax/100)/4);
  mpu.setYAccelOffset(-(ay/100)/4);
  mpu.setZAccelOffset((16384-(az/100))/4);
  mpuCalibrated = true;
  Serial.println("MPU6050 Calibrated!");
  bluetooth.println("MPU6050 Calibrated!");
}

// ---------- Main loop ----------
void loop() {
  if (mpuCalibrated) checkTamper();
  checkSensors();         // reads BME once and sets bmeOK + last* values
  handleBuzzer();
  processBluetoothInput();
  processSerialInput();   // NEW: handle Serial Monitor / USB input for GPS
  char key = customKeypad.getKey();
  if (key) handleKey(key);

  if (millis() - lastBTUpdate > 5000) {
    sendBTFullStatus();
    lastBTUpdate = millis();
  }

  updateDisplay();
  printSerialMenu();
  delay(80);
}

// ---------- Tamper detection ----------
void checkTamper() {
  int16_t ax, ay, az;
  mpu.getAcceleration(&ax, &ay, &az);
  float accelX = ax / 16384.0;
  float accelY = ay / 16384.0;
  float accelZ = az / 16384.0;

  bool tilt90 = abs(accelZ) < 0.3;
  bool shakeX = abs(accelX) > 1.5;
  bool shakeY = abs(accelY) > 1.5;
  bool shakeZ = abs(accelZ - 1.0) > 0.8;

  if (tilt90 || shakeX || shakeY || shakeZ) {
    if (!tamperDetected) {
      tamperDetected = true;
      Serial.println("🚨 TAMPER DETECTED!");
      bluetooth.println("🚨 TAMPER DETECTED!");
      buzzerActive = true;
      buzzerStartTime = millis();
    }
  }
}

// ---------- Sensors check (single read, update globals) ----------
void checkSensors() {
  // performReading once per loop and store values for use elsewhere
  if (bme.performReading()) {
    lastTemp = bme.temperature;
    lastHum  = bme.humidity;
    lastGas  = bme.gas_resistance / 1000.0; // KOhm
    bmeOK = true;

    // Threshold check using stored values
    if (lastTemp > tempThreshold || lastHum > humThreshold || lastGas < gasThreshold) {
      if (!buzzerActive) {
        buzzerActive = true;
        buzzerStartTime = millis();
        Serial.println("⚠ SENSOR ALERT!");
        bluetooth.println("⚠ SENSOR ALERT!");
      }
    }
  } else {
    // No valid reading this loop
    bmeOK = false;
  }
}

// ---------- Buzzer ----------
void handleBuzzer() {
  if (buzzerActive) {
    unsigned long elapsed = millis() - buzzerStartTime;
    if (elapsed < 15000) {
      int freq = 800 + (elapsed % 400);
      tone(BUZZER_PIN, freq, 200);
    } else {
      buzzerActive = false;
      noTone(BUZZER_PIN);
    }
  }
}

// ---------- Bluetooth input (improved) ----------
void processBluetoothInput() {
  while (bluetooth.available()) {
    String line = bluetooth.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    Serial.print("BT RX: ");
    Serial.println(line);

    // Convert to uppercase for safe comparison
    String U = line;
    U.toUpperCase();

    // ---- CASE 1: GPS: prefix ----
    if (U.startsWith("GPS:")) {
      String payload = line.substring(line.indexOf(':') + 1);
      payload.trim();

      lastGPS = payload;
      gpsFetching = false;

      Serial.print("GPS Updated (BT): ");
      Serial.println(lastGPS);

      bluetooth.print("GPS STORED:");
      bluetooth.println(lastGPS);

      continue;
    }

    // ---- CASE 2: During fetching → ANY TEXT = GPS ----
    if (gpsFetching) {
      lastGPS = line;
      gpsFetching = false;

      Serial.print("GPS Updated (fetch mode BT): ");
      Serial.println(lastGPS);

      bluetooth.print("GPS STORED:");
      bluetooth.println(lastGPS);

      continue;
    }

    // ---- CASE 3: Plain text with space/comma → treat as GPS ----
    if (line.indexOf(' ') != -1 || line.indexOf(',') != -1) {
      lastGPS = line;

      Serial.print("GPS Updated (plain BT): ");
      Serial.println(lastGPS);

      bluetooth.print("GPS STORED:");
      bluetooth.println(lastGPS);

      continue;
    }

    // ---- IF REQUEST PINS ----
    if (U == "REQUEST_PINS") {
      bluetooth.print("STORED_PIN:");
      bluetooth.println(pinCode);

      bluetooth.print("RANDOM_PIN:");
      bluetooth.println(randomPin);

      showTemporaryMessage("PINS SENT (BT)", "", 800);
      continue;
    }

    // other bluetooth commands (keep or extend)...
  }
}

// ---------- Serial input (NEW) ----------
void processSerialInput() {
  while (Serial.available()) {

    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    Serial.print("SERIAL RX: ");
    Serial.println(line);

    String U = line;
    U.toUpperCase();

    // --- CASE 1: Has prefix GPS: ---
    if (U.startsWith("GPS:")) {
      lastGPS = line.substring(line.indexOf(':') + 1);
      lastGPS.trim();
      gpsFetching = false;

      Serial.print("GPS Updated (Serial): ");
      Serial.println(lastGPS);
      continue;
    }

    // --- CASE 2: If in fetching mode, ANY text becomes GPS ---
    if (gpsFetching) {
      lastGPS = line;
      lastGPS.trim();
      gpsFetching = false;

      Serial.print("GPS Updated (fetch mode Serial): ");
      Serial.println(lastGPS);
      continue;
    }

    // --- CASE 3: Plain text containing spaces or comma -> treat as GPS ---
    if (line.indexOf(' ') != -1 || line.indexOf(',') != -1) {
      lastGPS = line;
      lastGPS.trim();

      Serial.print("GPS Updated (plain Serial): ");
      Serial.println(lastGPS);
      continue;
    }

    // other serial commands can be handled here...
  }
}

// ---------- Key handling ----------
void handleKey(char key) {
  bluetooth.print("Key:"); bluetooth.println(key);

  // universal back
  if (key == '*') {
    switch (currentMenu) {
      case MAIN_MENU: break;
      case STATUS_MENU: currentMenu = MAIN_MENU; break;
      case STATUS_DEVICE_LOCK: currentMenu = STATUS_MENU; break;
      case STATUS_SECURITY_MENU: currentMenu = STATUS_MENU; break;
      case STATUS_SECURITY_SHOWMASK: currentMenu = STATUS_SECURITY_MENU; break;
      case STATUS_SECURITY_CHANGE_OLD:
        changeOldBuffer = "";
        currentMenu = STATUS_SECURITY_MENU;
        break;
      case STATUS_SECURITY_CHANGE_NEW:
        changeNewBuffer = "";
        currentMenu = STATUS_SECURITY_MENU;
        break;
      case STATUS_GPS_MODE: currentMenu = STATUS_MENU; break;
      case STATUS_SENSOR_THRESHOLD_MENU: currentMenu = STATUS_MENU; break;
      case THRESHOLD_TEMP:
      case THRESHOLD_HUM:
      case THRESHOLD_GAS:
        inputBuffer = "";
        currentMenu = STATUS_SENSOR_THRESHOLD_MENU;
        break;
      case SENSOR_STATUS: currentMenu = MAIN_MENU; break;
      case UNLOCK_MENU: currentMenu = MAIN_MENU; break;
      case ENTER_PIN: inputBuffer = ""; currentMenu = UNLOCK_MENU; break;
      case RANDOM_MENU: currentMenu = UNLOCK_MENU; break;
      case RANDOM_GENERATE: currentMenu = RANDOM_MENU; break;
      case RANDOM_ENTER: inputBuffer = ""; currentMenu = RANDOM_MENU; break;
      case ABOUT_US: currentMenu = STATUS_MENU; break;
      case RESTARTING: currentMenu = MAIN_MENU; break;
      default: currentMenu = MAIN_MENU; break;
    }
    return;
  }

  // MAIN MENU
  if (currentMenu == MAIN_MENU) {
    if (key == 'A') { currentMenu = STATUS_MENU; inputBuffer = ""; bluetooth.println("→ Status Menu"); }
    else if (key == 'B') { restartSystem(); }
    else if (key == 'C') { currentMenu = SENSOR_STATUS; bluetooth.println("→ Sensors"); }
    else if (key == 'D') { currentMenu = UNLOCK_MENU; bluetooth.println("→ Unlock"); inputBuffer = ""; }
    return;
  }

  // STATUS MENU
  if (currentMenu == STATUS_MENU) {
    if (key == '1') { currentMenu = STATUS_DEVICE_LOCK; bluetooth.print("Device:"); bluetooth.println(locked ? "LOCKED" : "UNLOCKED"); }
    else if (key == '2') { currentMenu = STATUS_SECURITY_MENU; }
    else if (key == '3') { currentMenu = STATUS_GPS_MODE; }
    else if (key == '4') { currentMenu = STATUS_SENSOR_THRESHOLD_MENU; }
    else if (key == '5') { currentMenu = ABOUT_US; }
    return;
  }

  // SECURITY MENU
  if (currentMenu == STATUS_SECURITY_MENU) {
    if (key == '1') {
      // Change PIN flow: enter old pin
      changeOldBuffer = "";
      currentMenu = STATUS_SECURITY_CHANGE_OLD;
    } else if (key == '2') {
      // send stored + random pin
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_profont12_mf);
      u8g2.drawStr(0, 24, "SENDING...");
      u8g2.sendBuffer();

      bluetooth.print("STORED_PIN:"); bluetooth.println(pinCode);
      bluetooth.print("RANDOM_PIN:"); bluetooth.println(randomPin);
      Serial.println("PINs sent to phone via Bluetooth");

      delay(250);
      u8g2.clearBuffer();
      u8g2.drawStr(0, 24, "SENT!");
      u8g2.sendBuffer();
      delay(700);

      currentMenu = STATUS_SECURITY_MENU;
      updateDisplay();
    }
    return;
  }

  // Change PIN: entering old PIN
  if (currentMenu == STATUS_SECURITY_CHANGE_OLD) {
    if (key >= '0' && key <= '9') {
      if (changeOldBuffer.length() < 4) changeOldBuffer += key;
      return;
    }
    if (key == '#') {
      if (changeOldBuffer.length() == 4 && changeOldBuffer == pinCode) {
        // old PIN correct -> proceed to new PIN
        changeNewBuffer = "";
        currentMenu = STATUS_SECURITY_CHANGE_NEW;
        showTemporaryMessage("OLD PIN OK", "", 400);
      } else {
        // wrong old pin
        showTemporaryMessage("WRONG PIN!", "", 700);
        changeOldBuffer = "";
        currentMenu = STATUS_SECURITY_MENU;
      }
      return;
    }
    return;
  }

  // Change PIN: entering new PIN
  if (currentMenu == STATUS_SECURITY_CHANGE_NEW) {
    if (key >= '0' && key <= '9') {
      if (changeNewBuffer.length() < 4) changeNewBuffer += key;
      return;
    }
    if (key == '#') {
      // confirm and save
      if (changeNewBuffer.length() == 4) {
        pinCode = changeNewBuffer;
        saveEEPROMAll();
        bluetooth.println("PIN_CHANGED");
        showTemporaryMessage("PIN Saved!", "", 900);
        changeOldBuffer = "";
        changeNewBuffer = "";
        currentMenu = STATUS_SECURITY_MENU;
        updateDisplay();
      } else {
        showTemporaryMessage("PIN must be 4 digits", "", 900);
        changeNewBuffer = "";
        currentMenu = STATUS_SECURITY_MENU;
      }
      return;
    }
    return;
  }

  // GPS MODE
  if (currentMenu == STATUS_GPS_MODE) {
    if (key == '#') {
      bluetooth.println("GPS_REQUEST");
      Serial.println("GPS_REQUEST sent to phone.");
      gpsFetching = true;
      gpsFetchStart = millis();
      showTemporaryMessage("Fetching GPS...", "", 400);
    }
    return;
  }

  // Threshold menu
  if (currentMenu == STATUS_SENSOR_THRESHOLD_MENU) {
    if (key == '1') { inputBuffer = ""; currentMenu = THRESHOLD_TEMP; }
    else if (key == '2') { inputBuffer = ""; currentMenu = THRESHOLD_HUM; }
    else if (key == '3') { inputBuffer = ""; currentMenu = THRESHOLD_GAS; }
    return;
  }

  // Threshold entry
  if (currentMenu == THRESHOLD_TEMP || currentMenu == THRESHOLD_HUM || currentMenu == THRESHOLD_GAS) {
    if ((key >= '0' && key <= '9') || key == '.') {
      inputBuffer += key;
    } else if (key == '#') {
      float val = inputBuffer.toFloat();
      if (currentMenu == THRESHOLD_TEMP) tempThreshold = val;
      else if (currentMenu == THRESHOLD_HUM) humThreshold = val;
      else if (currentMenu == THRESHOLD_GAS) gasThreshold = val;

      saveEEPROMAll();

      if (currentMenu == THRESHOLD_TEMP) bluetooth.print("Saved:Temp=");
      else if (currentMenu == THRESHOLD_HUM) bluetooth.print("Saved:Hum=");
      else bluetooth.print("Saved:Gas=");
      bluetooth.println(val);

      showTemporaryMessage("Saved!", "", 700);
      inputBuffer = "";
      currentMenu = STATUS_SENSOR_THRESHOLD_MENU;
      updateDisplay();
      delay(150);

      // immediate check using stored BME values
      if (bmeOK) {
        if (lastTemp > tempThreshold || lastHum > humThreshold || lastGas < gasThreshold) {
          if (!buzzerActive) {
            buzzerActive = true;
            buzzerStartTime = millis();
            Serial.println("⚠ SENSOR ALERT (post-save)!");
            bluetooth.println("⚠ SENSOR ALERT (post-save)!");
          }
        }
      }
    }
    return;
  }

  // UNLOCK menu
  if (currentMenu == UNLOCK_MENU) {
    if (key == '1') { inputBuffer = ""; currentMenu = ENTER_PIN; bluetooth.println("→ Enter PIN"); }
    else if (key == '2') { currentMenu = RANDOM_MENU; inputBuffer = ""; bluetooth.println("→ Random PIN"); }
    return;
  }

  // ENTER PIN (unlock)
  if (currentMenu == ENTER_PIN) {
    if (key >= '0' && key <= '9') {
      if (inputBuffer.length() < 4) inputBuffer += key;
      return;
    }
    if (key == '#') {
      String entered = inputBuffer;
      entered.trim();
      String stored = pinCode;
      stored.trim();

      Serial.print("DEBUG Entered PIN: ["); Serial.print(entered); Serial.print("] len="); Serial.println(entered.length());
      Serial.print("DEBUG Stored  PIN: ["); Serial.print(stored);  Serial.print("] len="); Serial.println(stored.length());

      if (entered.length() == 4 && entered == stored) {
        unlockDoor();
        bluetooth.println("✅ PIN OK");
      } else {
        wrongPin();
        bluetooth.println("❌ WRONG PIN");
      }
      inputBuffer = "";
      currentMenu = MAIN_MENU;
      return;
    }
    return;
  }

  // RANDOM menu
  if (currentMenu == RANDOM_MENU) {
    if (key == '1') { generateRandomPin(); currentMenu = RANDOM_GENERATE; bluetooth.print("Random PIN:"); bluetooth.println(randomPin); }
    else if (key == '2') { inputBuffer = ""; currentMenu = RANDOM_ENTER; bluetooth.println("→ Enter Random PIN"); }
    return;
  }

  // RANDOM_ENTER
  if (currentMenu == RANDOM_ENTER) {
    if (key >= '0' && key <= '9') {
      if (inputBuffer.length() < 4) inputBuffer += key;
      return;
    }
    if (key == '#') {
      String entered = inputBuffer;
      entered.trim();
      if (entered.length() == 4 && entered == randomPin) {
        unlockDoor();
        bluetooth.println("✅ RANDOM PIN OK");
      } else {
        wrongPin();
        bluetooth.println("❌ WRONG RANDOM PIN");
      }
      inputBuffer = "";
      currentMenu = MAIN_MENU;
      return;
    }
    return;
  }
}

// ---------- Random PIN ----------
void generateRandomPin() {
  randomSeed(analogRead(A0) ^ millis());
  randomPin = "";
  for (int i = 0; i < 4; ++i) randomPin += String(random(0, 10));
  Serial.print("Random PIN generated: "); Serial.println(randomPin);
}

// ---------- Wrong PIN ----------
void wrongPin() {
  tone(BUZZER_PIN, 600, 500);
  Serial.println("❌ WRONG PIN!");
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont12_mf);
  u8g2.drawStr(30, 30, "WRONG PIN!");
  u8g2.sendBuffer();
  delay(800);
}

// ---------- Unlock door ----------
void unlockDoor() {
  digitalWrite(RELAY_PIN, LOW); // unlock
  locked = false;
  Serial.println("✅ DOOR UNLOCKED");
  bluetooth.println("✅ DOOR UNLOCKED");

  tone(BUZZER_PIN, 2000);
  delay(3000);
  noTone(BUZZER_PIN);

  digitalWrite(RELAY_PIN, HIGH);
  locked = true;
  Serial.println("🔒 AUTO-RELOCKED");
  bluetooth.println("🔒 AUTO-RELOCKED");
}

// ---------- Restart ----------
void restartSystem() {
  currentMenu = RESTARTING;
  updateDisplay();
  Serial.println("🔄 RESTARTING...");
  bluetooth.println("🔄 RESTART");
  tone(BUZZER_PIN, 1500, 500);
  delay(1000);
  asm volatile ("  jmp 0");
}

// ---------- BT status updates ----------
void sendBTSystemStatus() {
  bluetooth.println("=== EVIDENCE BOX ===");
  bluetooth.print("Lock:"); bluetooth.println(locked ? "LOCKED" : "OPEN");
  bluetooth.print("PIN:"); bluetooth.println("");
  bluetooth.print("T:"); bluetooth.print(tempThreshold); bluetooth.println("C");
  bluetooth.print("H:"); bluetooth.print(humThreshold); bluetooth.println("%");
  bluetooth.print("G:"); bluetooth.println(gasThreshold);
  bluetooth.print("GPS:"); bluetooth.println(lastGPS);
  bluetooth.println("===================");
}

void sendBTFullStatus() {
  // Use stored values (no repeated performReading)
  if (bmeOK) {
    bluetooth.print("Live-T:"); bluetooth.print(lastTemp);
    bluetooth.print(" H:"); bluetooth.print(lastHum);
    bluetooth.print(" G:"); bluetooth.println(lastGas);
  } else {
    bluetooth.println("Live: Sensor N/A");
  }
}

// ---------- Error display ----------
void errorDisplay(String msg) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont12_mf);
  u8g2.drawStr(0, 20, msg.c_str());
  u8g2.sendBuffer();
}

// ---------- Serial menu (dbg) ----------
void printSerialMenu() {
  Serial.println("========================================");
  Serial.println(" SECURE EVIDENCE SAFETY BOX");
  Serial.println("========================================");

  switch (currentMenu) {
    case MAIN_MENU:
      Serial.println(" A - Status");
      Serial.println(" B - Restart");
      Serial.println(" C - Sensors");
      Serial.println(" D - Unlock");
      break;
    case STATUS_MENU:
      Serial.println(" 1 - Device Lock Status");
      Serial.println(" 2 - Security / PIN Info");
      Serial.println(" 3 - GPS Mode");
      Serial.println(" 4 - Sensor Threshold");
      Serial.println(" 5 - About Us");
      Serial.println(" * - Back");
      break;
    default:
      break;
  }

  Serial.println("========================================");
}

// ---------- OLED update ----------
void updateDisplay() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont12_mf);

  // =============== MAIN MENU ===============
  if (currentMenu == MAIN_MENU) {
    u8g2.drawStr(0, 10, "SECURE EVIDENCE BOX");
    u8g2.drawStr(0, 25, "A: Status");
    u8g2.drawStr(0, 35, "B: Restart");
    u8g2.drawStr(0, 45, "C: Sensors");
    u8g2.drawStr(0, 55, "D: Unlock");
    u8g2.sendBuffer();
    return;
  }

  // =============== STATUS MENU ===============
  if (currentMenu == STATUS_MENU) {
    u8g2.drawStr(0, 10, "STATUS MENU");
    u8g2.drawStr(0, 25, "1: Device Lock");
    u8g2.drawStr(0, 35, "2: Security / PIN");
    u8g2.drawStr(0, 45, "3: GPS Mode");
    u8g2.drawStr(0, 55, "4: Sensor Threshold");
    u8g2.drawStr(90, 55, "5: About Us");
    u8g2.sendBuffer();
    return;
  }

  // =============== DEVICE LOCK ===============
  if (currentMenu == STATUS_DEVICE_LOCK) {
    u8g2.drawStr(0, 20, "Device Lock Status:");
    u8g2.drawStr(0, 40, locked ? "LOCKED" : "UNLOCKED");
    u8g2.drawStr(0, 58, "*: Back");
    u8g2.sendBuffer();
    return;
  }

  // =============== SECURITY MENU ===============
  if (currentMenu == STATUS_SECURITY_MENU) {
    u8g2.drawStr(0, 10, "SECURITY / PIN INFO");
    u8g2.drawStr(0, 25, "1: Change PIN");
    u8g2.drawStr(0, 35, "2: Send PIN to Phone");
    u8g2.drawStr(0, 58, "*: Back");
    u8g2.sendBuffer();
    return;
  }

  // =============== ENTER OLD PIN ===============
  if (currentMenu == STATUS_SECURITY_CHANGE_OLD) {
    u8g2.drawStr(0, 10, "Enter Old PIN:");
    String masked = "";
    for (uint8_t i = 0; i < changeOldBuffer.length(); i++) masked += "*";
    u8g2.drawStr(0, 30, masked.c_str());
    u8g2.drawStr(0, 58, "#:Confirm  *:Back");
    u8g2.sendBuffer();
    return;
  }

  // =============== ENTER NEW PIN ===============
  if (currentMenu == STATUS_SECURITY_CHANGE_NEW) {
    u8g2.drawStr(0, 10, "Enter New PIN:");
    String masked = "";
    for (uint8_t i = 0; i < changeNewBuffer.length(); i++) masked += "*";
    u8g2.drawStr(0, 30, masked.c_str());
    u8g2.drawStr(0, 58, "#:Save  *:Back");
    u8g2.sendBuffer();
    return;
  }

  // =============== GPS MODE SCREEN ===============
  if (currentMenu == STATUS_GPS_MODE) {
    u8g2.drawStr(0, 10, "GPS MODE");
    u8g2.drawStr(0, 24, "Last GPS:");

    if (gpsFetching) {
      u8g2.drawStr(0, 38, "Fetching...");
    } else {
      String gpsDisplay = lastGPS;
      if (gpsDisplay.length() > 20)
        gpsDisplay = gpsDisplay.substring(0, 20);
      u8g2.drawStr(0, 38, gpsDisplay.c_str());
    }

    u8g2.drawStr(0, 58, "#: Fetch GPS  *=Back");
    u8g2.sendBuffer();
    return;
  }

  // =============== THRESHOLD MENU ===============
  if (currentMenu == STATUS_SENSOR_THRESHOLD_MENU) {
    char b1[25], b2[25], b3[25];
    sprintf(b1, "1: Temp  %.1fC", tempThreshold);
    sprintf(b2, "2: Hum   %.1f%%", humThreshold);
    sprintf(b3, "3: Gas   %.1fK", gasThreshold);

    u8g2.drawStr(0, 10, "THRESHOLD MENU");
    u8g2.drawStr(0, 25, b1);
    u8g2.drawStr(0, 36, b2);
    u8g2.drawStr(0, 48, b3);
    u8g2.drawStr(0, 58, "*: Back");
    u8g2.sendBuffer();
    return;
  }

  // =============== THRESHOLD INPUT ===============
  if (currentMenu == THRESHOLD_TEMP ||
      currentMenu == THRESHOLD_HUM ||
      currentMenu == THRESHOLD_GAS) {

    if (currentMenu == THRESHOLD_TEMP)
      u8g2.drawStr(0, 20, "Enter Temp:");

    if (currentMenu == THRESHOLD_HUM)
      u8g2.drawStr(0, 20, "Enter Humidity:");

    if (currentMenu == THRESHOLD_GAS)
      u8g2.drawStr(0, 20, "Enter Gas:");

    u8g2.drawStr(0, 40, inputBuffer.c_str());
    u8g2.drawStr(0, 58, "#:Save  *=Back");
    u8g2.sendBuffer();
    return;
  }

  // =============== SENSOR STATUS ===============
  if (currentMenu == SENSOR_STATUS) {
    u8g2.drawStr(0, 10, "SENSOR VALUES:");

    if (bmeOK) {
      char buf[30];

      sprintf(buf, "Temp: %.1fC", lastTemp);
      u8g2.drawStr(0, 25, buf);

      sprintf(buf, "Hum : %.1f%%", lastHum);
      u8g2.drawStr(0, 38, buf);

      sprintf(buf, "Gas : %.1fK", lastGas);
      u8g2.drawStr(0, 51, buf);

    } else {
      u8g2.drawStr(0, 35, "Sensor Error");
    }

    u8g2.drawStr(0, 63, "*: Back");
    u8g2.sendBuffer();
    return;
  }

  // =============== UNLOCK MENU ===============
  if (currentMenu == UNLOCK_MENU) {
    u8g2.drawStr(0, 10, "UNLOCK MENU");
    u8g2.drawStr(0, 25, "1: Enter PIN");
    u8g2.drawStr(0, 35, "2: Random PIN");
    u8g2.drawStr(0, 55, "*: Back");
    u8g2.sendBuffer();
    return;
  }

  // =============== ENTER PIN ===============
  if (currentMenu == ENTER_PIN) {
    u8g2.drawStr(0, 20, "Enter PIN:");

    String masked = "";
    for (uint8_t i = 0; i < inputBuffer.length(); i++) masked += "*";
    u8g2.drawStr(0, 40, masked.c_str());

    u8g2.drawStr(0, 58, "#:Enter  *:Back");
    u8g2.sendBuffer();
    return;
  }

  // =============== RANDOM MENU ===============
  if (currentMenu == RANDOM_MENU) {
    u8g2.drawStr(0, 15, "RANDOM PIN MENU");
    u8g2.drawStr(0, 35, "1: Generate");
    u8g2.drawStr(0, 45, "2: Enter Random PIN");
    u8g2.drawStr(0, 63, "*: Back");
    u8g2.sendBuffer();
    return;
  }

  // =============== RANDOM GENERATED PIN ===============
  if (currentMenu == RANDOM_GENERATE) {
    u8g2.drawStr(0, 15, "RANDOM PIN:");
    u8g2.drawStr(0, 35, randomPin.c_str());
    u8g2.drawStr(0, 63, "*: Back");
    u8g2.sendBuffer();
    return;
  }

  // =============== RANDOM ENTER PIN ===============
  if (currentMenu == RANDOM_ENTER) {
    u8g2.drawStr(0, 20, "Enter Random PIN:");
    String mask2 = "";
    for (uint8_t i = 0; i < inputBuffer.length(); i++) mask2 += "*";
    u8g2.drawStr(0, 40, mask2.c_str());
    u8g2.drawStr(0, 58, "#:Enter  *:Back");
    u8g2.sendBuffer();
    return;
  }

  // =============== ABOUT US ===============
  if (currentMenu == ABOUT_US) {
    u8g2.drawStr(0, 8, "HUTTHESH BS");
    u8g2.drawStr(0, 22, "HITESHGOWDA H");
    u8g2.drawStr(0, 36, "OJUS A");
    u8g2.drawStr(0, 50, "VINOD KUMAR V");
    u8g2.drawStr(0, 62, "*: Back");
    u8g2.sendBuffer();
    return;
  }

  // =============== RESTARTING ===============
  if (currentMenu == RESTARTING) {
    u8g2.drawStr(20, 30, "Restarting.....");
    u8g2.sendBuffer();
    return;
  }
}

// ---------- end of sketch ----------
