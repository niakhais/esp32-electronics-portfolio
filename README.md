# Electronics Portfolio

A collection of ESP32-based embedded systems projects, documented honestly — including what didn't work and why. Statuses below are accurate as of the last time each project was tested, not aspirational.

## Projects

| Project | Status | What it demonstrates |
|---|---|---|
| [Universal IR Remote](./tvremote/) | ✅ Complete, working | Protocol decoding, flash persistence, ported third-party code integration, edge-triggered debounce debugging |

More projects added as they're physically rebuilt and verified.

## Common lessons that recur across projects
A few things were learned once and then deliberately reused, rather than re-discovered project to project:
- **Edge-triggered vs. level-triggered debouncing** — first found as the root cause of "phantom" button behavior in the IR remote.
- **The ESP32 Arduino core 3.x PWM API change** (`ledcAttach`/pin-addressed `ledcWrite`, replacing the older `ledcSetup`/`ledcAttachPin`/channel-addressed calls).
- **Verify wiring with a scanner or multimeter before trusting a diagram.**
- **Test the riskiest assumption cheaply before building the mechanism around it.**
