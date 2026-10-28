// ESP32 Thermistor Fan Controller — linear PWM curve version
// Adapted from github.com/FAB1150/ESP32_fan_control, rebuilt for this hardware:
//   Thermistor divider -> GPIO34 (ADC1, input-only)
//   Fan PWM (through PN2222 driver stage) -> GPIO26
//   OLED SSD1306 128x64 -> SDA 21 / SCL 22
//
// Key difference from the reference repo: real fail-safe behavior.
// If the sensor reading is implausible (open circuit / short), this
// does NOT silently show a wrong temperature — it forces the fan to
// full speed and flags SENSOR FAULT on the OLED, same as your original.

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- Pins ----------
#define THERMISTOR_PIN 34   // ADC1, input-only — correct pin for analogRead
#define FAN_PIN        26   // PWM out, through PN2222 base resistor
#define SDA_PIN        21
#define SCL_PIN        22

// ---------- OLED ----------
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------- Thermistor math ----------
#define FIXED_R   10000.0   // fixed resistor in the divider, ohms
#define THERM_R0  10000.0   // thermistor resistance at 25C
#define THERM_B   3950.0    // beta coefficient
#define ADC_MAX   4095.0
#define V_SUPPLY  3.3
#define V_OFFSET  0.0        // calibrate this — see note below

// ---------- Fan curve (linear, like the reference repo) ----------
#define MINTEMP       27.0   // below this: fan at MIN_DUTY (or off)
#define MAXTEMP       45.0   // at/above this: fan at MAX_DUTY
#define MIN_DUTY      80     // 0-255 scale; raise if your fan stalls at low duty
#define MAX_DUTY      255
#define FAN_OFF_BELOW_MIN false  // true = fan stops below MINTEMP instead of idling at MIN_DUTY

// ---------- Fail-safe ----------
#define ADC_FAULT_LOW   50    // near-zero reading = likely short to GND
#define ADC_FAULT_HIGH  4045  // near-max reading = likely open circuit

// ---------- PWM ----------
#define PWM_FREQ 25000  // 25kHz, above audible range, standard for 4-wire fans
#define PWM_RES  8      // 8-bit, 0-255

#define DEBUG true      // set false once calibrated

unsigned long lastRead = 0;
const unsigned long READ_INTERVAL = 1000;

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed");
    while (true) delay(1000);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  ledcAttach(FAN_PIN, PWM_FREQ, PWM_RES);  // core 3.x API

  analogReadResolution(12);
  analogSetPinAttenuation(THERMISTOR_PIN, ADC_11db); // full 0-3.3V range
}

void loop() {
  if (millis() - lastRead < READ_INTERVAL) return;
  lastRead = millis();

  int raw = analogRead(THERMISTOR_PIN);

  bool fault = (raw <= ADC_FAULT_LOW) || (raw >= ADC_FAULT_HIGH);

  float tempC = 0;
  int duty = 0;

  if (fault) {
    // Fail-safe: don't trust a bad reading, don't silently show garbage.
    duty = MAX_DUTY;
    ledcWrite(FAN_PIN, duty);

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("SENSOR FAULT");
    display.println("Fan: FULL SPEED");
    display.setCursor(0, 30);
    display.print("ADC raw: ");
    display.println(raw);
    display.display();

    if (DEBUG) {
      Serial.print("FAULT raw=");
      Serial.println(raw);
    }
    return;
  }

  // raw -> voltage -> resistance -> temperature (beta equation)
  float voltage = (raw / ADC_MAX) * V_SUPPLY + V_OFFSET;
  float rTherm = FIXED_R * (V_SUPPLY / voltage - 1.0);
  float tempK = 1.0 / (1.0 / (THERM_R0_TO_K()) + (1.0 / THERM_B) * log(rTherm / THERM_R0));
  tempC = tempK - 273.15;

  // Linear fan curve between MINTEMP and MAXTEMP
  if (tempC <= MINTEMP) {
    duty = FAN_OFF_BELOW_MIN ? 0 : MIN_DUTY;
  } else if (tempC >= MAXTEMP) {
    duty = MAX_DUTY;
  } else {
    float frac = (tempC - MINTEMP) / (MAXTEMP - MINTEMP);
    duty = MIN_DUTY + (int)(frac * (MAX_DUTY - MIN_DUTY));
  }

  ledcWrite(FAN_PIN, duty);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(2);
  display.print(tempC, 1);
  display.println(" C");
  display.setTextSize(1);
  display.setCursor(0, 30);
  display.print("Fan duty: ");
  display.print(duty);
  display.print("/255");
  display.setCursor(0, 45);
  display.print(duty == 0 ? "OFF" : (tempC >= MAXTEMP ? "MAX" : "RAMPING"));
  display.display();

  if (DEBUG) {
    Serial.print("raw="); Serial.print(raw);
    Serial.print(" V="); Serial.print(voltage, 3);
    Serial.print(" R="); Serial.print(rTherm, 0);
    Serial.print(" tempC="); Serial.print(tempC, 2);
    Serial.print(" duty="); Serial.println(duty);
  }
}

// 25C in Kelvin, kept as a function so the constant is obvious
float THERM_R0_TO_K() {
  return 298.15; // 25C + 273.15
}
