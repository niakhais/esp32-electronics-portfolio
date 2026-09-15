#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <Preferences.h>
#include "WORLD_IR_CODES.h"

// ---------- Pin definitions ----------
#define PIN_IR_RECV   27
#define PIN_IR_SEND   26
#define PIN_BTN_LEARN 32
#define PIN_BTN_BLAST 33
#define PIN_BTN_NAV   25

// ---------- OLED ----------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------- IR ----------
const uint16_t kCaptureBufferSize = 1024;
const uint8_t kTimeout = 15;
IRrecv irrecv(PIN_IR_RECV, kCaptureBufferSize, kTimeout, true);
IRsend irsend(PIN_IR_SEND);
decode_results results;

// ---------- Storage ----------
Preferences prefs;
const uint16_t kMaxRaw = 200;

bool hasStoredCode = false;
bool storedIsKnownProtocol = false;
decode_type_t storedProtocol;
uint64_t storedValue = 0;
uint16_t storedBits = 0;
uint16_t storedRaw[kMaxRaw];
uint16_t storedRawLen = 0;

// ---------- Mode ----------
enum Mode { IDLE, LEARNING };
Mode mode = IDLE;
unsigned long learnStartTime = 0;
const unsigned long LEARN_TIMEOUT_MS = 10000;

// ---------- Button debounce (edge-triggered, not level-triggered) ----------
struct Button {
  uint8_t pin;
  bool lastReading;
  bool stableState;
  unsigned long lastChangeTime;
  unsigned long lowSince;
};

const unsigned long DEBOUNCE_MS = 40;
const unsigned long STUCK_PIN_MS = 5000;
const unsigned long NAV_LONG_MS = 1000;

Button btnLearn = {PIN_BTN_LEARN, HIGH, HIGH, 0, 0};
Button btnBlast = {PIN_BTN_BLAST, HIGH, HIGH, 0, 0};
Button btnNav   = {PIN_BTN_NAV,   HIGH, HIGH, 0, 0};

unsigned long navPressStart = 0;
bool navLongTriggered = false;

enum ButtonEvent { BTN_NONE, BTN_PRESSED, BTN_RELEASED };

ButtonEvent updateButton(Button &b) {
  bool reading = digitalRead(b.pin);
  if (reading != b.lastReading) {
    b.lastChangeTime = millis();
    b.lastReading = reading;
  }
  ButtonEvent event = BTN_NONE;
  if (millis() - b.lastChangeTime > DEBOUNCE_MS && reading != b.stableState) {
    b.stableState = reading;
    if (b.stableState == LOW) {
      event = BTN_PRESSED;
      b.lowSince = millis();
    } else {
      event = BTN_RELEASED;
    }
  }
  return event;
}

bool isStuck(Button &b) {
  return (b.stableState == LOW) && (millis() - b.lowSince > STUCK_PIN_MS);
}

// ---------- TV-B-Gone decompression engine ----------
static const uint8_t *codePtr;
static uint8_t bitsLeft = 0;
static uint8_t curByte = 0;

uint16_t readBits(uint8_t numbits) {
  uint16_t d = 0;
  while (numbits) {
    if (bitsLeft == 0) {
      curByte = *codePtr;
      codePtr++;
      bitsLeft = 8;
    }
    d <<= 1;
    d |= (curByte >> (bitsLeft - 1)) & 1;
    bitsLeft--;
    numbits--;
  }
  return d;
}

void sendTvbGoneCode(const IrCode *code) {
  static uint16_t rawBuf[600];
  uint16_t idx = 0;

  codePtr = code->codes;
  bitsLeft = 0;

  for (uint8_t k = 0; k < code->numpairs; k++) {
    uint16_t ti = readBits(code->bitcompression) * 2;
    rawBuf[idx++] = code->times[ti]     * 10;
    rawBuf[idx++] = code->times[ti + 1] * 10;
  }

  uint16_t freqKHz = code->timer_val / 1000;
  irsend.sendRaw(rawBuf, idx, freqKHz);
}

void blastAllCodes(const IrCode * const table[], uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    sendTvbGoneCode(table[i]);
    delay(120);
    yield();
  }
}

// ---------- Display helper ----------
void showMessage(String line1, String line2 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(line1);
  display.println(line2);
  display.display();
}

// ---------- Preferences load/save ----------
void loadStoredCode() {
  prefs.begin("irremote", true);
  hasStoredCode = prefs.getBool("hasCode", false);
  if (hasStoredCode) {
    storedIsKnownProtocol = prefs.getBool("known", false);
    if (storedIsKnownProtocol) {
      storedProtocol = (decode_type_t)prefs.getUInt("protocol", 0);
      storedValue = prefs.getULong64("value", 0);
      storedBits = prefs.getUShort("bits", 0);
    } else {
      storedRawLen = prefs.getUShort("rawlen", 0);
      if (storedRawLen > kMaxRaw) storedRawLen = kMaxRaw;
      prefs.getBytes("rawdata", storedRaw, storedRawLen * sizeof(uint16_t));
    }
  }
  prefs.end();
}

void saveLearnedCode() {
  prefs.begin("irremote", false);
  prefs.putBool("hasCode", true);

  if (results.decode_type != UNKNOWN && results.decode_type != UNUSED) {
    prefs.putBool("known", true);
    prefs.putUInt("protocol", (uint32_t)results.decode_type);
    prefs.putULong64("value", results.value);
    prefs.putUShort("bits", results.bits);
    storedIsKnownProtocol = true;
    storedProtocol = results.decode_type;
    storedValue = results.value;
    storedBits = results.bits;
  } else {
    prefs.putBool("known", false);
    uint16_t len = results.rawlen - 1;
    if (len > kMaxRaw) len = kMaxRaw;
    for (uint16_t i = 0; i < len; i++) {
      storedRaw[i] = results.rawbuf[i + 1] * kRawTick;
    }
    storedRawLen = len;
    prefs.putUShort("rawlen", len);
    prefs.putBytes("rawdata", storedRaw, len * sizeof(uint16_t));
    storedIsKnownProtocol = false;
  }

  prefs.end();
  hasStoredCode = true;
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_BTN_LEARN, INPUT_PULLUP);
  pinMode(PIN_BTN_BLAST, INPUT_PULLUP);
  pinMode(PIN_BTN_NAV, INPUT_PULLUP);
  delay(50); // let pull-ups settle before reading

  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
    for (;;);
  }

  // Boot-time wiring check
  bool learnLow = (digitalRead(PIN_BTN_LEARN) == LOW);
  bool blastLow = (digitalRead(PIN_BTN_BLAST) == LOW);
  bool navLow   = (digitalRead(PIN_BTN_NAV) == LOW);
  if (learnLow || blastLow || navLow) {
    String which = learnLow ? "LEARN" : (blastLow ? "BLAST" : "NAV");
    Serial.print("BOOT CHECK: ");
    Serial.print(which);
    Serial.println(" pin reads LOW at startup - check wiring");
    showMessage("Wiring warning", which + " reads LOW");
    delay(2000);
  }

  irrecv.enableIRIn();
  irsend.begin();

  loadStoredCode();

  showMessage("IR Remote Ready", hasStoredCode ? "Code stored" : "No code yet");
}

// ---------- Loop ----------
void loop() {
  ButtonEvent learnEvent = updateButton(btnLearn);
  ButtonEvent blastEvent = updateButton(btnBlast);
  ButtonEvent navEvent   = updateButton(btnNav);

  bool stuckLearn = isStuck(btnLearn);
  bool stuckBlast = isStuck(btnBlast);
  bool stuckNav   = isStuck(btnNav);
  bool anyStuck = stuckLearn || stuckBlast || stuckNav;

  static bool wasFaulted = false;
  if (anyStuck) {
    if (!wasFaulted) {
      String which = stuckLearn ? "LEARN" : (stuckBlast ? "BLAST" : "NAV");
      Serial.print("HARDWARE FAULT: ");
      Serial.print(which);
      Serial.println(" pin stuck LOW - check wiring/short");
      showMessage("HARDWARE FAULT", which + " stuck LOW");
      wasFaulted = true;
    }
    return; // don't process any button logic while a pin is faulted
  } else if (wasFaulted) {
    wasFaulted = false;
    Serial.println("Fault cleared");
    showMessage("Fault cleared", "Resuming...");
    delay(800);
    showMessage("IR Remote Ready", hasStoredCode ? "Code stored" : "No code yet");
  }

  // --- Learn ---
  if (learnEvent == BTN_PRESSED) {
    Serial.println("LEARN pressed");
    mode = LEARNING;
    learnStartTime = millis();
    showMessage("Learning...", "Point remote & press");
  }

  if (mode == LEARNING) {
    if (irrecv.decode(&results)) {
      Serial.print("IR decoded, protocol: ");
      Serial.println(typeToString(results.decode_type));
      saveLearnedCode();
      String proto = (results.decode_type != UNKNOWN) ?
                      String(typeToString(results.decode_type)) : "RAW";
      showMessage("Learned!", proto);
      irrecv.resume();
      mode = IDLE;
      delay(1200);
      showMessage("IR Remote Ready", "Code stored");
    } else if (millis() - learnStartTime > LEARN_TIMEOUT_MS) {
      Serial.println("Learn timed out");
      mode = IDLE;
      showMessage("Learn timed out", "Try again");
      delay(1000);
      showMessage("IR Remote Ready", hasStoredCode ? "Code stored" : "No code yet");
    }
  }

  // --- Blast ---
  if (blastEvent == BTN_PRESSED) {
    Serial.println("BLAST pressed");
    if (hasStoredCode) {
      showMessage("Sending...");
      if (storedIsKnownProtocol) {
        irsend.send(storedProtocol, storedValue, storedBits);
      } else {
        irsend.sendRaw(storedRaw, storedRawLen, 38);
      }
      delay(400);
      showMessage("IR Remote Ready", "Code stored");
    } else {
      showMessage("No code stored", "Learn one first");
      delay(1000);
      showMessage("IR Remote Ready", "No code yet");
    }
  }

  // --- Nav: short press = status, long press (1s+) = blast full database ---
  if (navEvent == BTN_PRESSED) {
    navPressStart = millis();
    navLongTriggered = false;
  }

  if (btnNav.stableState == LOW && navPressStart != 0 && !navLongTriggered &&
      millis() - navPressStart > NAV_LONG_MS) {
    navLongTriggered = true;
    Serial.println("NAV long-press: blasting DB");
    showMessage("Blasting DB...", "Aim at TV");
    blastAllCodes(NApowerCodes, num_NAcodes);
    showMessage("Blast complete");
    delay(1000);
    showMessage("IR Remote Ready", hasStoredCode ? "Code stored" : "No code yet");
  }

  if (navEvent == BTN_RELEASED) {
    if (!navLongTriggered) {
      Serial.println("NAV short-press: status");
      if (hasStoredCode) {
        String proto = storedIsKnownProtocol ?
                        String(typeToString(storedProtocol)) : "RAW";
        showMessage("Stored code:", proto);
      } else {
        showMessage("No code stored");
      }
      delay(1000);
      showMessage("IR Remote Ready", hasStoredCode ? "Code stored" : "No code yet");
    }
    navPressStart = 0;
  }
}