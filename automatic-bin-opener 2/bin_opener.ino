#define TRIG_PIN 26
#define ECHO_PIN 14
#define SERVO_PIN 27

#include <ESP32Servo.h>
Servo lidServo;

const int CLOSED_ANGLE = 0;
const int OPEN_ANGLE = 90; // tune once mechanically mounted
const int STEP_DELAY_MS = 15; // time between each 1-degree servo step

const int TRIGGER_DISTANCE_CM = 20; // tune to your real swipe distance
const int STABLE_READS_NEEDED = 3;
const unsigned long OPEN_DURATION = 10000; // 10 seconds
const unsigned long SENSOR_SETTLE_MS = 65; // HC-SR04 needs ~60ms+ between pings to avoid echo interference

const int NO_ECHO = -1; // sentinel: sensor timed out, reading is inconclusive, not "far away"

enum LidState { CLOSED, OPENING, OPEN, CLOSING };
LidState state = CLOSED;

int consecutiveCloseReads = 0;
unsigned long openedAt = 0;
int currentServoAngle = CLOSED_ANGLE;
unsigned long lastStepTime = 0;
unsigned long lastMeasureTime = 0;

long measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return NO_ECHO; // timeout — inconclusive, not a confirmed "far" reading
  return duration * 0.0343 / 2;
}

// moves servo one step closer to target, non-blocking. returns true once target reached.
bool stepTowards(int target) {
  if (millis() - lastStepTime < STEP_DELAY_MS) return false;
  lastStepTime = millis();

  if (currentServoAngle < target) currentServoAngle++;
  else if (currentServoAngle > target) currentServoAngle--;

  lidServo.write(currentServoAngle);
  return currentServoAngle == target;
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  lidServo.attach(SERVO_PIN);
  lidServo.write(CLOSED_ANGLE);
  currentServoAngle = CLOSED_ANGLE;
}

void loop() {
  // Only take a new sensor reading every SENSOR_SETTLE_MS — this replaces a blanket
  // delay() with a non-blocking timer, so the servo stepping (which needs to run every
  // loop iteration during OPENING/CLOSING) is never held up waiting on the sensor.
  long dist = NO_ECHO;
  bool gotNewReading = false;
  if (millis() - lastMeasureTime >= SENSOR_SETTLE_MS) {
    dist = measureDistance();
    lastMeasureTime = millis();
    gotNewReading = true;
    Serial.println(dist); // temporary diagnostic
  }

  switch (state) {
    case CLOSED:
      if (gotNewReading) {
        if (dist != NO_ECHO && dist < TRIGGER_DISTANCE_CM) {
          consecutiveCloseReads++;
        } else if (dist != NO_ECHO) {
          // a genuine "far" reading — reset. A timeout does NOT reset; it's inconclusive.
          consecutiveCloseReads = 0;
        }
      }
      if (consecutiveCloseReads >= STABLE_READS_NEEDED) {
        state = OPENING;
        consecutiveCloseReads = 0;
        Serial.println("STATE -> OPENING");
      }
      break;

    case OPENING:
      if (stepTowards(OPEN_ANGLE)) {
        state = OPEN;
        openedAt = millis();
        Serial.println("STATE -> OPEN");
      }
      break;

    case OPEN:
      if (gotNewReading && dist != NO_ECHO && dist < TRIGGER_DISTANCE_CM) {
        openedAt = millis(); // re-swipe resets the close timer
      }
      if (millis() - openedAt >= OPEN_DURATION) {
        state = CLOSING;
        Serial.println("STATE -> CLOSING");
      }
      break;

    case CLOSING:
      if (stepTowards(CLOSED_ANGLE)) {
        state = CLOSED;
        Serial.println("STATE -> CLOSED");
      }
      break;
  }
}
