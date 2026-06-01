# Architecture Overview

High-level architecture of the tracked robot firmware.

## System Overview

```
┌─────────────────────────────────────────────────────────────┐
│                     ESP32-WROOM-32                           │
│                                                              │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐            │
│  │    PS4     │  │   Serial   │  │    HTTP    │            │
│  │ Controller │  │ Controller │  │ Controller │            │
│  └─────┬──────┘  └─────┬──────┘  └─────┬──────┘            │
│        │               │               │                    │
│        └───────────────┴───────────────┘                    │
│                        │                                     │
│                 ┌──────▼──────┐                             │
│                 │   Control   │                             │
│                 │   Manager   │  (Arbitration)              │
│                 └──────┬──────┘                             │
│                        │                                     │
│                 ┌──────▼──────┐                             │
│                 │   Safety    │                             │
│                 │  & Failsafe │                             │
│                 └──────┬──────┘                             │
│                        │                                     │
│                 ┌──────▼──────┐                             │
│                 │ Differential│                             │
│                 │    Mixer    │                             │
│                 └──────┬──────┘                             │
│                        │                                     │
│                 ┌──────▼──────┐                             │
│                 │    Motor    │                             │
│                 │   Control   │                             │
│                 └──────┬──────┘                             │
│                        │                                     │
└────────────────────────┼────────────────────────────────────┘
                         │
         ┌───────────────┴───────────────┐
         │                               │
    ┌────▼────┐                     ┌────▼────┐
    │ IBT-2   │                     │ IBT-2   │
    │  Left   │                     │  Right  │
    └────┬────┘                     └────┬────┘
         │                               │
    ┌────▼────┐                     ┌────▼────┐
    │  Motor  │                     │  Motor  │
    │  Left   │                     │  Right  │
    └─────────┘                     └─────────┘
```

## Component Breakdown

### 1. Control Sources

All three sources produce a standardized `control_frame_t` and submit it via
`control_manager_submit()`.

#### PS4 Controller (`controller_ps4.c` + `ps4.c`)

- **Transport**: Bluetooth Classic (Bluepad32 + BTstack — replaces ESP-IDF `esp_hidh`)
- **Discovery**: BTstack auto-scan + autoconnect on boot; accepts any gamepad
- **Pairing**: Hold PS+Share on controller until light bar flashes rapidly
- **Button mapping**:
  - Left stick Y → throttle (inverted: up = forward)
  - Left stick X → steering
  - Options → arm
  - Cross → emergency stop
  - L1 → slow mode

#### Serial Controller (`controller_serial.c`)

- **Transport**: UART (115200 baud, 8N1)
- **Protocol**: JSON lines (`{"throttle": 0.5, "steering": -0.2}`)
- **Use case**: External microcontroller, PC automation

#### HTTP Controller (`controller_http.c`)

- **Transport**: Wi-Fi (AP always active + optional STA fallback)
- **Protocol**: REST + tab-based web UI
- **Endpoints**: `POST /control`, `POST /estop`, `POST /estop-reset`, `POST /arm`,
  `GET /status`, `GET|POST /config`, `POST /wifi`, `POST /reboot`, `GET /`
- **Captive portal**: a DNS server answers every query with `192.168.4.1` and a
  404 handler 302-redirects unknown paths to the UI (see
  [captive-portal.md](captive-portal.md))

### 2. Control Manager (`control_manager.c`)

**Arbitration model**: "Last owner" — the most recently active source holds
control until it times out (500 ms default).

**Control loop** (50 Hz / 20 ms):
1. Check timeout → if expired, clear active source and zero frame
2. Handle e-stop → call `safety_emergency_stop()`
3. Handle arm request → call `safety_arm()`
4. Update watchdog
5. Mix and drive motors (only if armed)

### 3. Safety & Failsafe (`safety_failsafe.c`)

**States**:

```
DISARMED ──[arm]──→ ARMED ──[estop]──→ ESTOP
   ↑                  │                  │
   └──[timeout]───────┘    [arm]────────→ DISARMED
```

- **Boot default**: DISARMED — motors never move unexpectedly on power-up
- **Watchdog timeout**: Auto-disarm after 500 ms of no control input
- **E-stop**: Latched; requires explicit re-arm to clear

### 4. Differential Drive Mixer (`mixer_diffdrive.c`)

Converts `(throttle, steering)` → `(left_speed, right_speed)`.

```
1. Apply deadzone  (ignore inputs within ±deadzone of zero)
2. Apply expo      (finer control near centre)
3. Mix:  left  = throttle + steering
         right = throttle - steering
4. Clamp to [-1.0, +1.0]
5. Scale by max_speed
6. If slow_mode: scale by slow_mode_factor (default 50%)
```

### 5. Motor Control (`motor_bts7960.c`)

- Dual IBT-2 / BTS7960 H-bridge drivers
- 20 kHz PWM @ 12-bit (4096 steps)
- Slew-rate limiting (200 ms ramp — configurable)
- Emergency stop bypasses ramping for instant stop
- Per-motor direction inversion via Kconfig

### 6. PWM Driver (`pwm_ledc.c`)

- ESP32 LEDC peripheral (low-speed mode)
- Shared timer for all 4 channels
- Actual frequency: 80 MHz / 4096 = 19.53 kHz

### 7. Monitoring (`motor_monitor.c`)

- ADC1 oneshot sampling of the IBT-2 `R_IS`/`L_IS` current-sense pins (10 Hz)
  and an optional battery voltage divider (1 Hz)
- Latches an emergency stop via `motor_emergency_stop()` +
  `safety_emergency_stop()` on per-motor over-current
- Readings exposed in `GET /status` (`monitor` block) and the web UI
- ADC1 only (ADC2 is unavailable while Wi-Fi is active); see
  [monitoring.md](monitoring.md)

---

## Data Flow

```
PS4 / Serial / HTTP
        │
        ▼  control_frame_t { throttle, steering, estop, arm, slow_mode }
  Control Manager
        │
        ▼  safety check (armed? estop?)
   Safety Layer
        │
        ▼  (throttle, steering, slow_mode)
 Differential Mixer
        │
        ▼  (left_speed, right_speed) ∈ [-1.0, +1.0]
  Motor Control
        │
        ▼  PWM duty cycles
   LEDC channels
        │
        ▼
  IBT-2 drivers → Motors
```

---

## FreeRTOS Tasks

| Task | Priority | Stack | Purpose |
|------|----------|-------|---------|
| `control_task` | 5 | 4 KB | Main control loop (50 Hz) |
| `serial_task` | 4 | 4 KB | Serial JSON parsing |
| `motor_ramp_task` | 4 | 2 KB | Motor slew-rate limiter |
| `monitor` | 4 | 3 KB | Current-sense (10 Hz) + battery (1 Hz) sampling |
| `captive_dns` | 4 | 3 KB | Captive-portal DNS responder (port 53) |
| `bluepad32` | 5 | 12 KB | Bluepad32/BTstack run loop (replaces ps4_scan + hidh) |

---

## Configuration System

All user-tunable parameters are in `firmware/main/Kconfig.projbuild`
under the `Robot Configuration` menu. Defaults live in `sdkconfig.defaults`.

Key groups:

| Group | Examples |
|-------|---------|
| Motor Pins | RPWM, LPWM, R_EN, L_EN per motor |
| Motor Control | PWM frequency, resolution, ramp rate, invert |
| Differential Drive | Deadzone, expo, max speed, slow mode factor |
| Control Sources | Enable/disable PS4, Serial, HTTP |
| WiFi | SSID, password, AP/STA mode |
| Safety | Failsafe timeout, status LED pin |
| Robot Monitoring | Over-current threshold, current scaling, battery divider |

---

## Design Decisions

### No priority between control sources

All three sources (PS4, Serial, HTTP) are peers. The most recently active one
holds control. This is simple, predictable, and avoids silent conflicts.

### Boot disarmed

Motors never move on power-up regardless of pending inputs or reconnecting
controllers. The user must explicitly press Options (or send `{"arm": true}`)
to enable motors.

### 20 kHz PWM

Inaudible (above 20 kHz human hearing threshold), well within the BTS7960's
25 kHz limit, and 12-bit resolution gives 0.024% speed steps (very smooth).
See [PWM Tuning](pwm-tuning.md) for details.

### Bluepad32 + BTstack for PS4 (third-party gamepad library)

The PS4 DualShock 4 backend uses **Bluepad32** on top of **BTstack**, not
ESP-IDF's native `esp_hidh`. Bluedroid is disabled in `sdkconfig.defaults`.

**Why not esp_hidh?**
- `esp_hidh` requires manual HID report parsing (byte offsets for each button/axis)
- Bluepad32 provides a stable, tested gamepad abstraction for DS4, DS5, Xbox, etc.
- BTstack is more robust than Bluedroid for Classic BT HID gamepads

**Components added for Bluepad32**:
- `firmware/components/ps4/` — platform adapter + `ps4_gamepad_t` abstraction
- `firmware/components/cmd_nvs/` — stub required by Bluepad32's build system
- `firmware/components/cmd_system/` — stub required by Bluepad32's build system

---

*Last updated: 2026-05-09*
