#define TRIG_PIN 26
#define ECHO_PIN 14
#define SERVO_PIN 27
#define JOYSTICK_PIN 34
#define MODE_BUTTON_PIN 33

#include <ESP32Servo.h>
#include <WiFi.h>
#include <WebServer.h>

const char* ap_ssid = "ESP32Radar";
const char* ap_password = "radar1234"; // minimum 8 characters required

WebServer server(80);
Servo radarServo;

bool manualMode = false;
int sweepAngle = 0;
int sweepDir = 1;

bool lastRawReading = HIGH;
bool debouncedState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

int currentAngle = 0;
long currentDist = -1;

long measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return -1;
  return duration * 0.0343 / 2;
}

void checkModeButton() {
  bool reading = digitalRead(MODE_BUTTON_PIN);
  if (reading != lastRawReading) lastDebounceTime = millis();
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != debouncedState) {
      debouncedState = reading;
      if (debouncedState == LOW) manualMode = !manualMode;
    }
  }
  lastRawReading = reading;
}

const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>ESP32 Sonar Map</title>
<style>
  body { background:#111; color:#0f0; font-family:monospace; text-align:center; }
  canvas { background:#000; border:1px solid #0f0; margin-top:20px; }
  #readout { font-size:20px; margin-top:12px; }
</style>
</head>
<body>
<h2>ESP32 Sonar Radar</h2>
<canvas id="radar" width="500" height="300"></canvas>
<div id="readout">Angle: --&deg;  Distance: -- cm</div>
<script>
const canvas = document.getElementById('radar');
const ctx = canvas.getContext('2d');
const readout = document.getElementById('readout');
const cx = canvas.width / 2;
const cy = canvas.height;
const maxDist = 200;
let points = [];

function drawBase() {
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.strokeStyle = '#0f0';
  ctx.beginPath();
  ctx.arc(cx, cy, canvas.width/2 - 5, Math.PI, 2*Math.PI);
  ctx.stroke();
}

function polarToXY(angleDeg, dist) {
  const rad = angleDeg * Math.PI / 180;
  const r = Math.min(dist, maxDist) / maxDist * (canvas.width/2 - 5);
  const x = cx - r * Math.cos(rad);
  const y = cy - r * Math.sin(rad);
  return [x, y];
}

async function poll() {
  try {
    const res = await fetch('/data');
    const data = await res.json();
    if (data.dist > 0) {
      const [x, y] = polarToXY(data.angle, data.dist);
      points.push({x, y});
      if (points.length > 150) points.shift();
    }
    drawBase();
    ctx.fillStyle = '#0f0';
    points.forEach(p => ctx.fillRect(p.x-1, p.y-1, 2, 2));

    const [sx, sy] = polarToXY(data.angle, maxDist);
    ctx.strokeStyle = '#0f0';
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(sx, sy);
    ctx.stroke();

    readout.textContent = `Angle: ${data.angle}°  Distance: ${data.dist > 0 ? data.dist + ' cm' : '--'}`;
  } catch (e) { console.log(e); }
  setTimeout(poll, 200);
}
drawBase();
poll();
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", htmlPage);
}

void handleData() {
  String json = "{\"angle\":" + String(currentAngle) + ",\"dist\":" + String(currentDist) + "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
  radarServo.attach(SERVO_PIN);

  WiFi.softAP(ap_ssid, ap_password);
  Serial.print("AP started. Connect to WiFi network: ");
  Serial.println(ap_ssid);
  Serial.print("Then browse to: http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
}

void loop() {
  server.handleClient();

  checkModeButton();

  if (manualMode) {
    int joyVal = analogRead(JOYSTICK_PIN);
    currentAngle = map(joyVal, 0, 4095, 0, 180);
    radarServo.write(currentAngle);
    delay(20);
  } else {
    radarServo.write(sweepAngle);
    delay(150);
    sweepAngle += sweepDir * 5;
    if (sweepAngle >= 180 || sweepAngle <= 0) sweepDir *= -1;
    currentAngle = sweepAngle;
  }

  currentDist = measureDistance();
}
