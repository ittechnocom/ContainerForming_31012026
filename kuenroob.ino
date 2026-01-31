/*
 * LAB Name: ESP32 Keypad 4x4 + MAX6675 + LCD 20x4 + Relay Control
 * Features:
 * - Press 'D' to toggle relay 1 ON/OFF
 * - Press 'A' to set Min-Max temperature (with decimal point support)
 * - Press 'C' to start/stop relay 2 and auto temperature control
 * - Auto relay control based on temperature range
 * LCD I2C: SDA=16, SCL=17
 * MAX6675: SO=21, CS=22, SCK=23
 * Keypad: Rows=13,12,14,27 / Cols=26,25,33,32
 * Relay1: GPIO 19 (Manual control with D button)
 * Relay2: GPIO 18 (Auto control with temperature)
 */

#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "Max6675.h"

// LCD Configuration
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Keypad Configuration
#define ROWS 4
#define COLS 4
char keyMap[ROWS][COLS] = {
  {'1','2','3', 'A'},
  {'4','5','6', 'B'},
  {'7','8','9', 'C'},
  {'*','0','#', 'D'}
};
uint8_t rowPins[ROWS] = {13, 12, 14, 27};
uint8_t colPins[COLS] = {26, 25, 33, 32};
Keypad keypad = Keypad(makeKeymap(keyMap), rowPins, colPins, ROWS, COLS);

// MAX6675 Configuration
Max6675 ts(21, 22, 23);

// Relay Pins (Active LOW)
#define RELAY_MANUAL 19
#define RELAY_AUTO 18

// Variables
String inputString = "";
unsigned long lastTempUpdate = 0;
const unsigned long tempUpdateInterval = 500;
unsigned long messageStartTime = 0;
bool showingMessage = false;

// Temperature control variables
float minTemp = 25.0;
float maxTemp = 35.0;
bool relayManualState = false;
bool relayAutoState = false;
bool autoControlEnabled = false;

// Setting mode variables
enum SettingMode {
  NORMAL,
  SETTING_MIN,
  SETTING_MAX
};
SettingMode currentMode = NORMAL;

void setup() {
  Serial.begin(115200);
  
  // Setup Relay pins (Active LOW logic)
  pinMode(RELAY_MANUAL, OUTPUT);
  pinMode(RELAY_AUTO, OUTPUT);
  digitalWrite(RELAY_MANUAL, HIGH);
  digitalWrite(RELAY_AUTO, HIGH);
  
  // Initialize I2C for LCD
  Wire.begin(16, 17);
  
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  
  // Display welcome message
  lcd.setCursor(0, 0);
  lcd.print("====================");
  lcd.setCursor(0, 1);
  lcd.print("  TEMP CONTROLLER  ");
  lcd.setCursor(0, 2);
  lcd.print("     ESP32 V2.0    ");
  lcd.setCursor(0, 3);
  lcd.print("====================");
  delay(2000);
  
  ts.setOffset(0);
  updateDisplay();
}

void loop() {
  // Update temperature reading
  if (millis() - lastTempUpdate >= tempUpdateInterval) {
    lastTempUpdate = millis();
    updateTemperature();
  }
  
  // Clear temporary message after 2 seconds
  if (showingMessage && (millis() - messageStartTime >= 2000)) {
    showingMessage = false;
    updateDisplay();
  }
  
  // Check for keypad input
  char key = keypad.getKey();
  if (key) {
    handleKeyPress(key);
  }
}

void updateDisplay() {
  lcd.clear();
  
  // Line 0: Temperature with clear label
  lcd.setCursor(0, 0);
  lcd.print("TEMP: ");
  float celsius = ts.getCelsius();
  lcd.print(celsius, 1);
  lcd.print((char)223); // degree symbol
  lcd.print("C");
  
  // Show status indicator
  lcd.setCursor(15, 0);
  if (autoControlEnabled) {
    if (celsius < minTemp) {
      lcd.print("[<MIN]");
    } else if (celsius >= maxTemp) {
      lcd.print("[>MAX]");
    } else {
      lcd.print("[ OK ]");
    }
  }
  
  // Line 1: Temperature range with box
  lcd.setCursor(0, 1);
  lcd.print("SET: ");
  lcd.print(minTemp, 1);
  lcd.print((char)223);
  lcd.print(" ~ ");
  lcd.print(maxTemp, 1);
  lcd.print((char)223);
  lcd.print("C");
  
  // Line 2: Relay status with clear indicators
  lcd.setCursor(0, 2);
  lcd.print("R1:[");
  lcd.print(relayManualState ? "ON " : "OFF");
  lcd.print("] R2:[");
  lcd.print(relayAutoState ? "ON " : "OFF");
  lcd.print("]");
  
  // Auto mode indicator
  lcd.setCursor(16, 2);
  if (autoControlEnabled) {
    lcd.print("[GO]");
  } else {
    lcd.print("[--]");
  }
  
  // Line 3: Instructions or input based on mode
  lcd.setCursor(0, 3);
  if (currentMode == NORMAL) {
    lcd.print("A:Set C:Auto D:R1  ");
  } else if (currentMode == SETTING_MIN) {
    lcd.print("MIN:");
    lcd.print(inputString);
    lcd.print((char)223);
    lcd.print("C *:OK #:Cancel");
  } else if (currentMode == SETTING_MAX) {
    lcd.print("MAX:");
    lcd.print(inputString);
    lcd.print((char)223);
    lcd.print("C *:OK #:Cancel");
  }
}

void updateTemperature() {
  float celsius = ts.getCelsius();
  
  // Update only temperature value without clearing whole display
  if (currentMode == NORMAL && !showingMessage) {
    lcd.setCursor(6, 0);
    lcd.print("          ");
    lcd.setCursor(6, 0);
    lcd.print(celsius, 1);
    lcd.print((char)223);
    lcd.print("C");
    
    // Update status indicator
    lcd.setCursor(15, 0);
    if (autoControlEnabled) {
      if (celsius < minTemp) {
        lcd.print("[<MIN]");
      } else if (celsius >= maxTemp) {
        lcd.print("[>MAX]");
      } else {
        lcd.print("[ OK ]");
      }
    } else {
      lcd.print("     ");
    }
  }
  
  // Auto relay control based on temperature
  if (autoControlEnabled) {
    if (celsius >= maxTemp) {
      if (relayAutoState) {
        relayAutoState = false;
        digitalWrite(RELAY_AUTO, HIGH);
        updateRelayStatus();
        Serial.print("→ Relay2 OFF (Temp ");
        Serial.print(celsius, 1);
        Serial.println("°C >= MAX)");
      }
    } else if (celsius < minTemp) {
      if (!relayAutoState) {
        relayAutoState = true;
        digitalWrite(RELAY_AUTO, LOW);
        updateRelayStatus();
        Serial.print("→ Relay2 ON (Temp ");
        Serial.print(celsius, 1);
        Serial.println("°C < MIN)");
      }
    }
  }
  
  // Print to Serial (less frequent)
  static unsigned long lastSerialPrint = 0;
  if (millis() - lastSerialPrint >= 2000) {
    lastSerialPrint = millis();
    Serial.print("TEMP:");
    Serial.print(celsius, 1);
    Serial.print("°C | RANGE:");
    Serial.print(minTemp, 1);
    Serial.print("-");
    Serial.print(maxTemp, 1);
    Serial.print("°C | R1:");
    Serial.print(relayManualState ? "ON" : "OFF");
    Serial.print(" R2:");
    Serial.print(relayAutoState ? "ON" : "OFF");
    Serial.print(" | AUTO:");
    Serial.println(autoControlEnabled ? "ON" : "OFF");
  }
}

void updateRelayStatus() {
  lcd.setCursor(0, 2);
  lcd.print("                    ");
  lcd.setCursor(0, 2);
  lcd.print("R1:[");
  lcd.print(relayManualState ? "ON " : "OFF");
  lcd.print("] R2:[");
  lcd.print(relayAutoState ? "ON " : "OFF");
  lcd.print("]");
  
  lcd.setCursor(16, 2);
  if (autoControlEnabled) {
    lcd.print("[GO]");
  } else {
    lcd.print("[--]");
  }
}

void showMessage(String msg) {
  lcd.setCursor(0, 3);
  lcd.print("                    ");
  lcd.setCursor(0, 3);
  lcd.print(msg);
  showingMessage = true;
  messageStartTime = millis();
}

void handleKeyPress(char key) {
  Serial.print("KEY: ");
  Serial.println(key);
  
  if (currentMode == NORMAL) {
    if (key == 'D') {
      // Toggle manual relay
      relayManualState = !relayManualState;
      digitalWrite(RELAY_MANUAL, relayManualState ? LOW : HIGH);
      updateRelayStatus();
      
      showMessage(relayManualState ? "R1: MANUAL ON" : "R1: MANUAL OFF");
      Serial.println(relayManualState ? "✓ R1 MANUAL ON" : "✗ R1 MANUAL OFF");
    }
    else if (key == 'A') {
      // Enter temperature setting mode
      autoControlEnabled = false;
      relayAutoState = false;
      digitalWrite(RELAY_AUTO, HIGH);
      
      currentMode = SETTING_MIN;
      inputString = "";
      updateDisplay();
      
      Serial.println("\n╔════════════════════╗");
      Serial.println("║  SETTING MIN TEMP  ║");
      Serial.println("╚════════════════════╝");
      Serial.println("Enter value, then press *");
    }
    else if (key == 'C') {
      // Toggle auto control mode
      autoControlEnabled = !autoControlEnabled;
      
      if (autoControlEnabled) {
        float currentTemp = ts.getCelsius();
        relayAutoState = true;
        digitalWrite(RELAY_AUTO, LOW);
        
        showMessage("AUTO MODE STARTED!");
        
        Serial.println("\n╔════════════════════╗");
        Serial.println("║   AUTO STARTED!    ║");
        Serial.println("╚════════════════════╝");
        Serial.print("Current: ");
        Serial.print(currentTemp, 1);
        Serial.println("°C");
        Serial.print("Range: ");
        Serial.print(minTemp, 1);
        Serial.print("°C ~ ");
        Serial.print(maxTemp, 1);
        Serial.println("°C");
        Serial.println("Relay2: ON (Heating)");
      } else {
        relayAutoState = false;
        digitalWrite(RELAY_AUTO, HIGH);
        
        showMessage("AUTO STOPPED");
        
        Serial.println("\n╔════════════════════╗");
        Serial.println("║   AUTO STOPPED     ║");
        Serial.println("╚════════════════════╝");
        Serial.println("Press 'C' to resume");
      }
      
      updateDisplay();
    }
  }
  else if (currentMode == SETTING_MIN) {
    if (key >= '0' && key <= '9') {
      if (inputString.length() < 5) {
        inputString += key;
        lcd.setCursor(4, 3);
        lcd.print(inputString);
        lcd.print((char)223);
        lcd.print("C");
      }
    }
    else if (key == '#') {
      // Add decimal point
      if (inputString.indexOf('.') == -1 && inputString.length() > 0) {
        inputString += '.';
        lcd.setCursor(4, 3);
        lcd.print(inputString);
      }
    }
    else if (key == '*') {
      if (inputString.length() > 0) {
        minTemp = inputString.toFloat();
        
        if (minTemp < 0 || minTemp > 300) {
          showMessage("ERROR: Invalid MIN!");
          delay(1500);
          inputString = "";
          updateDisplay();
          Serial.println("✗ ERROR: MIN must be 0-300°C");
          return;
        }
        
        Serial.print("✓ MIN set to: ");
        Serial.print(minTemp, 1);
        Serial.println("°C");
        Serial.println("\n╔════════════════════╗");
        Serial.println("║  SETTING MAX TEMP  ║");
        Serial.println("╚════════════════════╝");
        Serial.println("Enter value, then press *");
        
        inputString = "";
        currentMode = SETTING_MAX;
        updateDisplay();
      }
    }
    else if (key == 'B') {
      // Cancel
      inputString = "";
      currentMode = NORMAL;
      updateDisplay();
      Serial.println("✗ Setting cancelled");
    }
  }
  else if (currentMode == SETTING_MAX) {
    if (key >= '0' && key <= '9') {
      if (inputString.length() < 5) {
        inputString += key;
        lcd.setCursor(4, 3);
        lcd.print(inputString);
        lcd.print((char)223);
        lcd.print("C");
      }
    }
    else if (key == '#') {
      // Add decimal point
      if (inputString.indexOf('.') == -1 && inputString.length() > 0) {
        inputString += '.';
        lcd.setCursor(4, 3);
        lcd.print(inputString);
      }
    }
    else if (key == '*') {
      if (inputString.length() > 0) {
        maxTemp = inputString.toFloat();
        
        if (maxTemp <= minTemp) {
          showMessage("ERROR: MAX<=MIN!");
          delay(1500);
          inputString = "";
          updateDisplay();
          Serial.println("✗ ERROR: MAX must be > MIN");
          return;
        }
        
        if (maxTemp > 300) {
          showMessage("ERROR: Invalid MAX!");
          delay(1500);
          inputString = "";
          updateDisplay();
          Serial.println("✗ ERROR: MAX must be < 300°C");
          return;
        }
        
        Serial.println("\n╔════════════════════════════╗");
        Serial.println("║  TEMP RANGE CONFIGURED!    ║");
        Serial.println("╚════════════════════════════╝");
        Serial.print("MIN: ");
        Serial.print(minTemp, 1);
        Serial.println("°C");
        Serial.print("MAX: ");
        Serial.print(maxTemp, 1);
        Serial.println("°C");
        Serial.println("\nRelay2: OFF (Standby)");
        Serial.println("Press 'C' to start AUTO");
        
        relayAutoState = false;
        digitalWrite(RELAY_AUTO, HIGH);
        
        inputString = "";
        currentMode = NORMAL;
        autoControlEnabled = false;
        updateDisplay();
        
        showMessage("READY! Press C");
      }
    }
    else if (key == 'B') {
      // Cancel
      inputString = "";
      currentMode = NORMAL;
      updateDisplay();
      Serial.println("✗ Setting cancelled");
    }
  }
}