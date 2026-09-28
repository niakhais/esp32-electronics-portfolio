# Electronics Portfolio

A collection of ESP32-based embedded systems projects, documented honestly — including what didn't work and why. Statuses below are accurate as of the last time each project was tested, not aspirational.

## Projects

| Project | Status | What it demonstrates |
|---|---|---|
| [Universal IR Remote](./universal-ir-remote/) | ✅ Complete, working | Protocol decoding, flash persistence, ported third-party code integration, edge-triggered debounce debugging |
| [Ultrasonic Radar with Live Web Map](./ultrasonic-radar/) | ✅ Complete, working | Sensor + servo control, embedded WiFi web server, real-time browser visualization with no external dependencies |
| [Automatic Bin Opener](./automatic-bin-opener/) | ✅ Complete, working | Non-blocking state machines, timeout-vs-signal distinction in sensor logic, safety-conscious slow-motion design |
| [3D Gyro Visualizer](./gyro-3d-visualizer/) | ✅ Complete, working | I2C IMU reading, complementary-filter sensor fusion, live 3D visualization served from the ESP32 with no external libraries |

More projects added as they're physically rebuilt and verified.

## Common lessons that recur across projects
A few things were learned once and then deliberately reused, rather than re-discovered project to project:
- **Edge-triggered vs. level-triggered debouncing** — first found as the root cause of "phantom" button behavior in the IR remote, then hit again in a different form in the radar project (a state variable overwritten one loop iteration too early).
- **The ESP32 Arduino core 3.x PWM API change** (`ledcAttach`/pin-addressed `ledcWrite`, replacing the older `ledcSetup`/`ledcAttachPin`/channel-addressed calls).
- **Verify wiring with a scanner or multimeter before trusting a diagram** — an I2C scanner sketch is faster than any amount of visual inspection.
- **Test the riskiest assumption cheaply before building the mechanism around it.**
- **Check the constraints of the deployment environment, not just the code** — an ESP32 in access-point mode has no internet route, so any CDN-hosted library fails silently.
- **A sensor timeout is not the same thing as "no object detected"** — treating a missed reading as a confirmed negative can silently break trigger logic, even when the sensor itself is working fine.
- **Axis sign corrections belong to a physical mount** — a Z-axis negation that was right for an upside-down IMU gave a 179° roll reading once the board lay flat. Re-verify signs whenever the mount changes.
