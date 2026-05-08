# Safety and Failsafe System

Safety mechanisms and failsafe behaviour for the tracked robot firmware.

## Safety States

### DISARMED (boot default)
- Motors disabled regardless of control input
- Safe for wiring, handling, and initial setup

### ARMED
- Motors enabled and responsive to control input
- Entered via **Options** button or `{"arm": true}` over Serial/HTTP

### ESTOP (Emergency Stop)
- Latched — does NOT automatically clear
- Motors stop immediately (bypasses slew-rate limiter)
- Re-arm required via **Options** button or `{"arm": true}`

## State Transitions

```
                        DISARMED  (boot)
                        │       ↑
                      [arm]  [timeout / disarm]
                        │       │
                        ▼       │
                         ARMED ──┘
                           │
                        [estop]
                           │
                           ▼
                          ESTOP ──[arm]──→ DISARMED
```

## Failsafe Mechanisms

### 1. Boot Disarmed
The system always starts in DISARMED state. No input, button press,
or reconnecting controller can cause the motors to move without an explicit
arm command.

### 2. Watchdog Timeout (500 ms)
If no control frame is received for 500 ms (configurable in Kconfig), the
active source is cleared and motors stop. This covers:
- PS4 controller going out of range
- Serial host crash
- HTTP client timeout

### 3. Emergency Stop
Triggered by:
- **PS4 Cross (✕) button**
- `POST /estop`
- `{"estop": true}` over Serial

The stop is **latched** — it does not clear automatically. Press **Options**
to re-arm after resolving the issue.

### 4. Control Arbitration
Only one source is active at a time. Switching sources requires the previous
source to time out first (natural timeout, no manual override needed).

---

## LED Status Patterns

| Pattern | State |
|---------|-------|
| Fast blink (100 ms) | Boot initialising |
| Slow blink (1 s) | Disarmed — waiting for arm |
| Solid ON | Armed and ready |
| Very fast blink (50 ms) | Emergency stop active |

---

## Test Procedures

### Test 1 — Boot Disarmed
1. Power on ESP32
2. Move left stick → motors must **not** move
3. Press **Options** → system arms
4. Move stick → motors respond

### Test 2 — Watchdog Timeout
1. Arm system (Options)
2. Move stick (motors running)
3. Release stick to centre — do **not** send further input
4. After ~600 ms motors should stop and system disarms

### Test 3 — Emergency Stop
1. Arm system
2. Drive with left stick
3. Press **Cross (✕)** → motors stop instantly
4. Move stick → motors do **not** respond (e-stop latched)
5. Press **Options** → system re-arms

### Test 4 — Controller Disconnect
1. Arm system with PS4 controller
2. Turn off controller (hold PS button ~10 s)
3. Within ~600 ms the system should disarm automatically

---

## Safety Best Practices

### Before First Power-On
- Visual inspection of all wiring
- Continuity test on all GND connections
- Verify buck converter output is 5.0 V ± 0.25 V
- Tracks off the ground

### During Testing
- Always test with tracks elevated first
- Start in slow mode (hold **L1**)
- Keep physical e-stop within reach
- Monitor IBT-2 temperature

### In Normal Operation
- Never rely solely on the firmware e-stop
- Install a physical switch on the battery positive line
- Monitor battery voltage (add warning in firmware v2.0)

---

## Known Limitations

| Limitation | Impact |
|-----------|--------|
| Software-only failsafe | ESP32 crash → motors may continue |
| 500 ms timeout | Latency between disconnect and stop |
| HTTP latency | Network delay can slow e-stop via Wi-Fi |
| No battery monitoring | Low-voltage motor stall not detected in v0.1 |

> **Mandatory for any field use**: install a physical emergency-stop switch
> on the battery positive line. The firmware e-stop is a convenience feature,
> not a safety device.

*Last updated: 2026-05-08*
