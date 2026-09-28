#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>

#define SDA_PIN 25
#define SCL_PIN 13
#define MPU_ADDR 0x68

const char* ap_ssid = "ESP32Gyro3D";
const char* ap_password = "gyro1234";

WebServer server(80);
float rollAngle = 0;
float pitchAngle = 0;
unsigned long lastTime = 0;

const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 3D Gyro</title>
<style>
  body { background:#0a0a0a; color:#0f0; font-family:monospace; text-align:center; margin:0; overflow:hidden; }
  h2 { padding-top:16px; margin:0 0 6px; }
  .scene { width:100%; height:360px; perspective:800px; perspective-origin:50% 35%;
           display:flex; align-items:center; justify-content:center; }
  .stage { width:260px; height:260px; position:relative; transform-style:preserve-3d; transform:rotateX(58deg); }
  .ground { position:absolute; top:0; left:0; right:0; bottom:0; border:1px solid #0a5;
            background-image: linear-gradient(#063 1px, transparent 1px), linear-gradient(90deg, #063 1px, transparent 1px);
            background-size:26px 26px; }
  .drone { position:absolute; left:20px; top:20px; width:220px; height:220px;
           transform-style:preserve-3d; transform:translateZ(50px); transition:transform 0.08s linear; }
  .arm { position:absolute; left:10px; top:106px; width:200px; height:8px; background:#0a0; border-radius:3px; }
  .arm1 { transform:rotate(45deg); }
  .arm2 { transform:rotate(-45deg); }
  .rotor { position:absolute; width:38px; height:38px; border:3px solid #0f0; border-radius:50%; background:rgba(0,255,0,0.10); }
  .rotor.front { border-color:#f44; background:rgba(255,68,68,0.15); }
  .r1 { left:17px;  top:17px; }
  .r2 { left:159px; top:17px; }
  .r3 { left:17px;  top:159px; }
  .r4 { left:159px; top:159px; }
  .body { position:absolute; left:82px; top:82px; width:56px; height:56px; background:#0f0; border-radius:8px; box-shadow:0 0 15px #0f0; }
  .nose { position:absolute; left:98px; top:30px; width:0; height:0;
          border-left:12px solid transparent; border-right:12px solid transparent; border-bottom:22px solid #f44; }
  #readout { font-size:18px; margin-top:8px; }
  .lbl { font-size:11px; margin-top:8px; color:#0a0; }
  .bar { width:220px; height:14px; background:#111; border:1px solid #0f0; margin:2px auto; position:relative; }
  .fill { height:100%; background:#0f0; position:absolute; left:50%; width:0; }
  .mid { position:absolute; left:50%; top:0; width:1px; height:100%; background:#0a0; }
</style>
</head>
<body>
<h2>ESP32 3D Gyro</h2>
<div class="scene">
  <div class="stage">
    <div class="ground"></div>
    <div class="drone" id="drone">
      <div class="arm arm1"></div>
      <div class="arm arm2"></div>
      <div class="rotor front r1"></div>
      <div class="rotor front r2"></div>
      <div class="rotor r3"></div>
      <div class="rotor r4"></div>
      <div class="nose"></div>
      <div class="body"></div>
    </div>
  </div>
</div>
<div id="readout">Roll: 0.0&deg;  Pitch: 0.0&deg;</div>
<div class="lbl">ROLL</div>
<div class="bar"><div class="mid"></div><div class="fill" id="rollBar"></div></div>
<div class="lbl">PITCH</div>
<div class="bar"><div class="mid"></div><div class="fill" id="pitchBar"></div></div>
<script>
const drone = document.getElementById('drone');
const readout = document.getElementById('readout');
const rollBar = document.getElementById('rollBar');
const pitchBar = document.getElementById('pitchBar');

function setBar(el, v) {
  const pct = Math.min(Math.abs(v), 45) / 45 * 50;
  el.style.width = pct + '%';
  el.style.left = (v >= 0 ? 50 : 50 - pct) + '%';
}

async function poll() {
  try {
    const res = await fetch('/data');
    const d = await res.json();

    // If the drone tilts the opposite way from the real board, flip that term's sign.
    drone.style.transform = `translateZ(50px) rotateX(${-d.pitch}deg) rotateY(${d.roll}deg)`;

    readout.textContent = `Roll: ${d.roll.toFixed(1)}°  Pitch: ${d.pitch.toFixed(1)}°`;
    setBar(rollBar, d.roll);
    setBar(pitchBar, d.pitch);
  } catch (e) { console.log(e); }
  setTimeout(poll, 60);
}
poll();
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", htmlPage);
}

void handleData() {
  String json = "{\"roll\":" + String(rollAngle) + ",\"pitch\":" + String(pitchAngle) + "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  byte wakeResult = Wire.endTransmission();
  Serial.print("Wake write result (0 = success): ");
  Serial.println(wakeResult);
  delay(50);

  WiFi.softAP(ap_ssid, ap_password);
  Serial.print("Connect to WiFi: ");
  Serial.println(ap_ssid);
  Serial.print("Then browse to: http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();

  lastTime = micros();
}

void loop() {
  server.handleClient();

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  byte gotBytes = Wire.requestFrom(MPU_ADDR, 14, true);
  if (gotBytes != 14) {          // bad read: skip this sample instead of feeding garbage to the filter
    lastTime = micros();
    return;
  }

  int16_t accX = Wire.read() << 8 | Wire.read();
  int16_t accY = Wire.read() << 8 | Wire.read();
  int16_t accZ = Wire.read() << 8 | Wire.read();
  Wire.read(); Wire.read(); // temperature, unused
  int16_t gyroX = Wire.read() << 8 | Wire.read();
  int16_t gyroY = Wire.read() << 8 | Wire.read();

  float accXg = accX / 16384.0;
  float accYg = accY / 16384.0;
  float accZg = accZ / 16384.0;
  float gyroXdps = gyroX / 131.0;
  float gyroYdps = gyroY / 131.0;

  // Board lying flat, chip side up: accZ is about +1g, so NO negation here.
  // (The stabilizer needed -accZ because that mount was upside-down.)
  float accelRoll  = atan2(accYg, accZg) * 180.0 / PI;
  float accelPitch = atan2(-accXg, sqrt(accYg * accYg + accZg * accZg)) * 180.0 / PI;

  unsigned long now = micros();
  float dt = (now - lastTime) / 1000000.0;
  lastTime = now;

  rollAngle  = 0.98 * (rollAngle  + gyroXdps * dt) + 0.02 * accelRoll;
  pitchAngle = 0.98 * (pitchAngle + gyroYdps * dt) + 0.02 * accelPitch;
}
