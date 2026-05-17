# CLAUDE.md — Development Guidelines for AI Assistants

This file provides instructions and conventions for AI assistants (like Claude) working on this repository. It ensures consistency, maintainability, and adherence to project goals.

**Current firmware version**: v0.1.0 (CI passing — awaiting hardware test)

---

## Project Goals

### Primary Goals
1. **Production-ready tracked robot firmware** for ESP32-WROOM-32
2. **Multiple control methods**: PS4 DualShock 4 (primary, via Bluepad32 + BTstack), Serial (UART), HTTP (Wi-Fi)
3. **Safety-first design**: E-stop, arming, failsafe timeout, initial disarmed state
4. **High-quality motor control**: 20 kHz PWM, smooth differential drive, configurable parameters
5. **Full automation**: CI/CD builds, releases, and deploys web flasher with zero manual steps
6. **Comprehensive documentation**: Every decision explained, every interface documented

### Non-Goals
- ✗ Not a general-purpose robot platform (scoped to tracked robots)
- ✗ Not supporting other MCUs (ESP32 only)
- ✗ Not supporting other motor drivers (BTS7960 / IBT-2 only)
- ✗ Not a learning framework (production code, not tutorial)

---

## Repository Structure

### Folder Layout

```
/
   README.md              # User-facing quick start
   CLAUDE.md              # This file (AI assistant guide)
   PROGRESS.md            # Current status, roadmap, open questions
   LICENSE                # MIT license
   .gitignore             # Standard ESP-IDF + web ignores
   .gitmodules            # Comment-only — no active submodules (PS3 removed in v0.1.0)

   docs/                  # All documentation
      architecture.md
      pwm-tuning.md
      bts7960-wiring.md
      ps4-setup.md
      serial-protocol.md
      http-api.md
      safety-failsafe.md
      cicd.md
      web-flasher.md

   firmware/              # ESP-IDF v5.1.2 project root
      CMakeLists.txt      # Bluepad32 auto-fetch + patch + component setup
      sdkconfig.defaults  # ESP32-WROOM-32 production defaults
      partitions.csv      # 4 MB custom partition table
      main/
         CMakeLists.txt
         main.c           # app_main() — boot sequence + subsystem init
         Kconfig.projbuild
         idf_component.yml
      components/
         ps3/             # Stub directory only — PS3 removed in v0.1.0
            README.md
         ps4/             # Bluepad32-based gamepad backend
            include/ps4.h
            ps4.c
            CMakeLists.txt
         cmd_nvs/         # Stub required by Bluepad32 build system
            include/cmd_nvs.h
            cmd_nvs_stub.c
            CMakeLists.txt
         cmd_system/      # Stub required by Bluepad32 build system
            include/cmd_system.h
            cmd_system_stub.c
            CMakeLists.txt
         control/
            include/
               control_frame.h
               control_manager.h
               controller_ps4.h
               controller_serial.h
               controller_http.h
            control_manager.c
            controller_ps4.c
            controller_serial.c
            controller_http.c
            CMakeLists.txt
         motor/
            include/
               motor_bts7960.h
               pwm_ledc.h
            motor_bts7960.c
            pwm_ledc.c
            CMakeLists.txt
         motion/
            include/
               mixer_diffdrive.h
            mixer_diffdrive.c
            CMakeLists.txt
         safety/
            include/
               safety_failsafe.h
            safety_failsafe.c
            CMakeLists.txt

   web-flasher/           # Static site for browser-based flashing
      index.html          # Installation + configuration guide
      styles.css
      app.js              # Loads manifest.json, shows version banner
      manifest.json       # Placeholder — auto-generated/overwritten by CI

   tools/                 # Helper scripts (optional)

   .github/
      ci-touch.txt        # Bump file to trigger CI without code changes
      latest-release-notes.md
      workflows/
         ci.yml           # Build on push/PR; rolling 'latest' release on main
         release.yml      # Build + release on version tags (v*.*.*)
         pages.yml        # Deploy web flasher to GitHub Pages
```

### Naming Conventions

- **Files**: `snake_case.c`, `snake_case.h`
- **Directories**: `lowercase` (no underscores)
- **Components**: `lowercase` (e.g., `control`, `motor`, `safety`)
- **Functions**: `component_action()` (e.g., `motor_bts7960_set_speed()`)
- **Types**: `component_type_t` (e.g., `control_frame_t`)
- **Defines**: `COMPONENT_CONSTANT` (e.g., `MOTOR_PWM_FREQ_HZ`)
- **Kconfig**: `CONFIG_ROBOT_*` prefix for all project options

---

## Coding Conventions

### C Code Style

- **Standard**: C11
- **Indentation**: 4 spaces (no tabs)
- **Braces**: K&R style (opening brace on same line)
- **Line length**: 100 characters max
- **Comments**: `//` for single-line, `/* */` for multi-line
- **Headers**: Include guards using `#pragma once`

#### Example:

```c
#pragma once

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Initialize the BTS7960 motor driver
 *
 * @param config Motor configuration structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t motor_bts7960_init(const motor_config_t *config);
```

### Logging

Use ESP-IDF logging macros:

```c
#include "esp_log.h"

static const char *TAG = "motor_bts7960";

ESP_LOGI(TAG, "Motor initialized: PWM freq=%d Hz", freq);
ESP_LOGW(TAG, "Motor speed clamped: %.2f -> %.2f", raw, clamped);
ESP_LOGE(TAG, "Failed to initialize PWM: %s", esp_err_to_name(ret));
```

**Log levels**:
- `ESP_LOGI`: Normal operation (startup, state changes)
- `ESP_LOGD`: Debug info (stick values, calculations)
- `ESP_LOGW`: Warnings (clamping, timeouts)
- `ESP_LOGE`: Errors (initialization failures, invalid params)

### Error Handling

- Return `esp_err_t` for all functions that can fail
- Check all return values — never ignore errors
- Fail fast: return early on error, don't continue with invalid state
- Log at `ESP_LOGE` level before returning any error

```c
esp_err_t ret = some_function();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to do something: %s", esp_err_to_name(ret));
    return ret;
}
```

---

## Architecture and Component Contracts

### Component Dependency Graph

```
main.c
├── control_manager      (50 Hz arbitration)
│   ├── control_frame    (normalized interface type)
│   ├── safety_failsafe  (state machine)
│   ├── mixer_diffdrive  (mixing algorithm)
│   └── motor_bts7960    (motor commands)
├── motor_bts7960        (IBT-2 dual H-bridge + ramping)
│   └── pwm_ledc         (ESP32 LEDC PWM)
├── mixer_diffdrive      (tank-style steering)
├── safety_failsafe      (watchdog + LED)
│   └── motor_bts7960    (emergency stop)
├── controller_serial    (UART JSON)
│   └── control_manager
├── controller_http      (WiFi AP/STA + REST API)
│   ├── control_manager
│   ├── safety_failsafe
│   └── NVS (config persistence)
├── controller_ps4       (PS4 button/stick mapping)
│   └── control_manager
└── ps4                  (Bluepad32 + BTstack gamepad backend)
```

### Control Frame (Interface Contract)

All control sources produce a normalized control frame:

```c
typedef enum {
    CONTROL_SOURCE_NONE   = 0,
    CONTROL_SOURCE_PS4    = 1,
    CONTROL_SOURCE_SERIAL = 2,
    CONTROL_SOURCE_HTTP   = 3,
} control_source_t;

typedef struct {
    float throttle;      // -1.0 (full reverse) to +1.0 (full forward)
    float steering;      // -1.0 (full left) to +1.0 (full right)
    bool estop;          // Emergency stop command
    bool arm;            // Arming command
    bool slow_mode;      // Slow mode toggle
    uint32_t timestamp;  // xTaskGetTickCount() — for timeout detection
} control_frame_t;
```

**Rules**:
- `throttle` and `steering` MUST be clamped to [-1.0, +1.0]
- `timestamp` MUST be updated on every new input
- Controller modules call `control_manager_submit(source_id, &frame)`

### Control Manager

- Runs at 50 Hz (20 ms loop) with mutex protection
- **Arbitration**: Last source to submit a frame wins
- **Timeout**: If no frame received within `CONFIG_ROBOT_FAILSAFE_TIMEOUT_MS` (default 500 ms), auto-disarms
- On each tick: checks timeout → handles e-stop → updates watchdog → mixes + applies speeds if armed

### Motor Configuration

```c
typedef struct {
    uint8_t  left_rpwm, left_lpwm, left_ren, left_len;
    uint8_t  right_rpwm, right_lpwm, right_ren, right_len;
    uint32_t pwm_freq_hz;       // default 20000
    uint8_t  pwm_resolution;    // default 11 (2048 steps)
    uint32_t ramp_rate_ms;      // default 200 ms (full range)
    bool     invert_left;       // flip left motor direction
    bool     invert_right;      // flip right motor direction
} motor_config_t;
```

Motor ramp task runs at 20 ms intervals and slews toward target speed. `motor_emergency_stop()` bypasses ramping and immediately zeroes both motors.

### Differential Drive Mixer

```c
typedef struct {
    float deadzone;         // [0.0–0.2] default 0.05 (5%)
    float expo;             // [0.0–1.0] default 0.30
    float max_speed;        // [0.0–1.0] default 1.0
    float slow_mode_factor; // [0.0–1.0] default 0.5
} mixer_config_t;
```

**Algorithm** (applied in order):
1. Deadzone: zero input if |value| < deadzone, else rescale remaining range
2. Expo curve: `out = expo * x³ + (1 - expo) * x`
3. Differential drive: `left = throttle + steering`, `right = throttle - steering`
4. Clamp to [-1.0, 1.0]
5. Scale by `max_speed`
6. Scale by `slow_mode_factor` when slow mode active

### Safety State Machine

```c
typedef enum {
    SAFETY_STATE_DISARMED = 0,  // Default on boot — motors off
    SAFETY_STATE_ARMED    = 1,  // Motors enabled
    SAFETY_STATE_ESTOP    = 2,  // Emergency stop — latched, reset via POST /estop-reset
} safety_state_t;
```

**LED patterns** (GPIO 2, configurable via `CONFIG_ROBOT_STATUS_LED_PIN`):
| State | Pattern |
|-------|---------|
| BOOT | Fast blink (100 ms on/off) for ~2 s |
| DISARMED | Slow blink (1 s on/off) |
| ARMED | Solid ON |
| ESTOP | Very fast blink (50 ms on/off) |

**Watchdog task**: 100 ms loop checks if armed + last update > 500 ms → auto-disarms.
`safety_update_watchdog()` is called by the control manager on every valid input frame.

### PS4 Controller Input Mapping

```c
typedef struct {
    float lx, ly, rx, ry;                         // Sticks [-1.0, 1.0]
    bool cross, circle, square, triangle;
    bool l1, r1, l2, r2;
    bool share, options, ps;
    bool dpad_up, dpad_down, dpad_left, dpad_right;
    bool connected;
} ps4_gamepad_t;
```

| PS4 Input | Robot Function |
|-----------|---------------|
| Left stick Y (inverted) | Throttle |
| Left stick X | Steering |
| Options (≡) | Arm |
| Cross (×) | Emergency stop |
| L1 | Slow mode (hold) |

PS4 init is deferred 10 s after boot (`ps4_init_task`) to allow WiFi to stabilize.

### Serial Controller (UART)

- **UART**: UART_NUM_0, 115200 baud (default, `CONFIG_ROBOT_SERIAL_BAUD`)
- **Protocol**: JSON lines (one command per newline-terminated line, 256-byte buffer)
- **Task**: 4 KB stack, priority 4

```json
{"throttle": 0.5, "steering": -0.2}
{"estop": true}
{"arm": true}
{"slow_mode": true}
```

### HTTP Controller (WiFi + REST API)

**WiFi AP** (always active):
- SSID: `TrackRobot-Setup`, Password: `trackrobot`
- IP: `192.168.4.1`, max 4 clients

**WiFi STA** (optional, stored in NVS `wifi_cfg` namespace — keys `ssid`, `password`):
- 15 s connection timeout → fallback to AP-only
- Auto-reconnect every 5 s

**REST Endpoints**:
| Method | Path | Purpose |
|--------|------|---------|
| GET | `/` | Embedded web UI (HTML tabs) |
| POST | `/control` | `{throttle, steering, slow_mode}` |
| POST | `/estop` | Trigger emergency stop |
| POST | `/estop-reset` | Clear e-stop → DISARMED (then re-arm to drive) |
| POST | `/arm` | Arm the robot |
| GET | `/status` | JSON system status |
| POST | `/wifi` | `{ssid, password}` → saved to NVS |
| GET | `/config` | Read drive params from NVS |
| POST | `/config` | Write drive params to NVS (reboot to apply) |
| POST | `/reboot` | System reboot |

### NVS Configuration Contract

Drive parameters — namespace `robot_cfg`:

| Key | Type | Range | Kconfig fallback |
|-----|------|-------|-----------------|
| `deadzone` | int32 (%) | 0–20 | `CONFIG_ROBOT_DRIVE_DEADZONE` |
| `expo` | int32 (%) | 0–100 | `CONFIG_ROBOT_DRIVE_EXPO` |
| `max_speed` | int32 (%) | 10–100 | `CONFIG_ROBOT_DRIVE_MAX_SPEED` |
| `slow_factor` | int32 (%) | 10–100 | `CONFIG_ROBOT_DRIVE_SLOW_MODE_FACTOR` |

`main.c` reads NVS at boot (before `mixer_diffdrive_init`) and overrides Kconfig defaults.
WiFi STA credentials — namespace `wifi_cfg` (keys: `ssid`, `password`).
Changes to drive params take effect after reboot.

### Adding a New Control Source

1. **Create module**: `firmware/components/control/controller_myname.c`
2. **Implement interface**:
   - `esp_err_t controller_myname_init(void)`
   - `void controller_myname_task(void *arg)` (FreeRTOS task)
   - Produce `control_frame_t` and call `control_manager_submit()`
3. **Register in Kconfig**: Add enable option in `main/Kconfig.projbuild`
4. **Initialize in main.c**: Call `controller_myname_init()` if enabled
5. **Document**: Add `docs/myname-protocol.md`
6. **Update**: PROGRESS.md, README.md, docs/architecture.md

---

## Bluetooth / Gamepad Backend

### Bluepad32 + BTstack

PS4 controller support uses **Bluepad32** on top of **BTstack**.

**Why Bluepad32?**
- Supports PS4, PS5, Xbox, Nintendo Switch controllers uniformly
- More stable than ESP-IDF's `esp_hidh` for gamepads
- No manual HID byte-offset parsing needed

**Build integration** (`firmware/CMakeLists.txt`):
- Bluepad32 is auto-downloaded via CMake FetchContent from `ricardoquesada/bluepad32` main branch
- A patch step fixes the Bluepad32 CMakeLists.txt for ESP-IDF 5.x (adds `driver` to REQUIRES)
- `cmd_nvs/` and `cmd_system/` stubs exist solely for Bluepad32's build system

**`sdkconfig.defaults`** disables Bluedroid (`CONFIG_BT_BLUEDROID_ENABLED=n`) and enables BTstack controller-only mode (`CONFIG_BT_CONTROLLER_ONLY=y`).

**DO NOT** re-enable Bluedroid or `esp_hidh` — they conflict with BTstack.

---

## Hardware Reference

### Target MCU

**ESP32-WROOM-32** — 4 MB flash, no PSRAM, dual-core Xtensa LX6, 240 MHz.

### Partition Table (`firmware/partitions.csv`)

| Name | Type | Offset | Size | Purpose |
|------|------|--------|------|---------|
| nvs | data | 0x9000 | 6 KB | Configuration storage |
| phy_init | data | 0xf000 | 1 KB | RF calibration |
| factory | app | 0x10000 | 1.9 MB | Main firmware |
| storage | data | 0x200000 | 2 MB | SPIFFS (future use) |

### Pin Mapping (Confirmed Wiring)

| ESP32 GPIO | Function | Module |
|-----------|----------|--------|
| 27 | RPWM | IBT-2 Left |
| 14 | LPWM | IBT-2 Left |
| 25 | R_EN | IBT-2 Left |
| 26 | L_EN | IBT-2 Left |
| 34 | R_IS (ADC, optional) | IBT-2 Left |
| 35 | L_IS (ADC, optional) | IBT-2 Left |
| 18 | RPWM | IBT-2 Right |
| 19 | LPWM | IBT-2 Right |
| 33 | R_EN | IBT-2 Right |
| 32 | L_EN | IBT-2 Right |
| 36 | R_IS (ADC, optional) | IBT-2 Right |
| 39 | L_IS (ADC, optional) | IBT-2 Right |
| 2 | Status LED | On-board |

---

## Configuration Management

### Kconfig (`firmware/main/Kconfig.projbuild`)

All user-configurable options use prefix `CONFIG_ROBOT_*`. Key sections:

**Motor Pins**: Left RPWM=27, LPWM=14, R_EN=25, L_EN=26; Right RPWM=18, LPWM=19, R_EN=33, L_EN=32

**Motor Control**:
- `CONFIG_ROBOT_MOTOR_PWM_FREQ_HZ`: default 20000, range 1000–40000
- `CONFIG_ROBOT_MOTOR_PWM_RESOLUTION`: default 11, range 8–14
- `CONFIG_ROBOT_MOTOR_RAMP_RATE_MS`: default 200, range 0–2000
- `CONFIG_ROBOT_MOTOR_LEFT_INVERT` / `CONFIG_ROBOT_MOTOR_RIGHT_INVERT`: boolean

**Differential Drive**:
- `CONFIG_ROBOT_DRIVE_DEADZONE`: default 5 (%)
- `CONFIG_ROBOT_DRIVE_EXPO`: default 30 (%)
- `CONFIG_ROBOT_DRIVE_MAX_SPEED`: default 100 (%)
- `CONFIG_ROBOT_DRIVE_SLOW_MODE_FACTOR`: default 50 (%)

**Control Sources**:
- `CONFIG_ROBOT_ENABLE_PS4`: default Y
- `CONFIG_ROBOT_ENABLE_SERIAL`: default Y
- `CONFIG_ROBOT_SERIAL_BAUD`: default 115200
- `CONFIG_ROBOT_ENABLE_HTTP`: default Y

**WiFi** (AP mode defaults):
- SSID: `TrackedRobot`, Password: `robot123`, Channel: 1, Max connections: 4
- STA mode is configured at runtime via HTTP `/wifi` endpoint (not Kconfig)

**Safety**:
- `CONFIG_ROBOT_FAILSAFE_TIMEOUT_MS`: default 500, range 100–5000
- `CONFIG_ROBOT_STATUS_LED_PIN`: default 2 (set -1 to disable)

### `sdkconfig.defaults`

Contains production defaults for ESP32-WROOM-32:
- 4 MB flash, DIO mode, 40 MHz
- Bluetooth Classic + BLE via BTstack; Bluedroid disabled
- `CONFIG_BLUEPAD32_MAX_DEVICES=4`
- WiFi Rx/Tx buffers configured
- FreeRTOS: 1000 Hz tick, dual-core
- Watchdog: 10 s timeout
- Log levels: DEBUG max, INFO default
- Performance optimization mode

**DO NOT** commit `sdkconfig` (generated file, user-specific).

---

## PWM Frequency Tuning

### Current Default: 20 kHz @ 11-bit

**Rationale**:
- BTS7960 max: ~25 kHz
- Above audible range (no motor whine)
- 11-bit = 2048 steps — smooth DC motor control
- 12-bit at 20 kHz is borderline on ESP32 LEDC; 11-bit is the reliable sweet spot
- Actual LEDC output: ~19.5 kHz (80 MHz / 4096 with divider rounding)

**Trade-off**: `frequency = 80 MHz / (2^resolution)` — higher frequency → lower resolution.

Do not exceed 25 kHz (BTS7960 limit). Update both `CONFIG_ROBOT_MOTOR_PWM_FREQ_HZ` and `CONFIG_ROBOT_MOTOR_PWM_RESOLUTION` together if changing. Document results in `docs/pwm-tuning.md`.

---

## CI/CD and Releases

### Workflow Overview

**ci.yml** — Firmware CI:
- **Triggers**: Push to `main` or `claude/**`; PR to `main`
- Installs ESP-IDF v5.1.2, runs `idf.py build` (target: esp32)
- Uploads artifacts (bootloader, partition-table, app binary) — 30-day retention
- **On every push to `main`**: Deletes previous `latest` release/tag, creates new rolling `latest` pre-release with current binaries (release notes from `.github/latest-release-notes.md`)

**release.yml** — Version Release:
- **Triggers**: Tag push matching `v*.*.*`
- Builds firmware, extracts version from tag
- Generates `manifest.json` with absolute download URLs
- Creates GitHub Release with binaries + manifest
- Release body includes manual `esptool.py` flash instructions

**pages.yml** — Web Flasher Deploy:
- **Triggers**: `workflow_run` after ci.yml on main (if successful); any published release; manual `workflow_dispatch`
- Downloads latest release binaries from GitHub API into `_site/`
- Generates `manifest.json` with relative paths (same-origin as web flasher)
- Deploys to GitHub Pages (single concurrent deployment)

### Creating a Versioned Release

```bash
git tag -a v1.0.0 -m "Release v1.0.0"
git push origin v1.0.0
```

CI will: build firmware → create release with binaries → trigger web flasher deploy.

**NEVER** manually create releases or upload binaries.

### Web Flasher Manifest Format

```json
{
  "name": "Tracked Robot Firmware",
  "version": "1.0.0",
  "builds": [{
    "chipFamily": "ESP32",
    "parts": [
      {"path": "bootloader.bin",      "offset": 4096},
      {"path": "partition-table.bin", "offset": 32768},
      {"path": "track-robot.bin",     "offset": 65536}
    ]
  }]
}
```

If partition offsets change (new scheme), update the generation scripts in `release.yml` and `pages.yml`.

---

## Key Function Reference

| Component | Function | Purpose |
|-----------|----------|---------|
| `control_manager` | `control_manager_init()` | Start 50 Hz control task + mutex |
| `control_manager` | `control_manager_submit(source, frame)` | Submit frame from any controller |
| `control_manager` | `control_manager_get_active_source()` | Query last active source |
| `motor_bts7960` | `motor_bts7960_init(config)` | Init PWM channels + enable pins + ramp task |
| `motor_bts7960` | `motor_set_speeds(left, right)` | Set target speeds [-1.0, +1.0] |
| `motor_bts7960` | `motor_emergency_stop()` | Immediate zero (bypasses ramp) |
| `pwm_ledc` | `pwm_ledc_init(config)` | Configure LEDC timer + channel |
| `pwm_ledc` | `pwm_ledc_set_duty(channel, duty)` | Set raw duty count |
| `mixer_diffdrive` | `mixer_diffdrive_init(config)` | Store mixer params |
| `mixer_diffdrive` | `mixer_diffdrive_mix(throttle, steering, slow_mode, &left, &right)` | Compute L/R speeds |
| `safety_failsafe` | `safety_failsafe_init()` | Start LED + watchdog tasks, boot pattern |
| `safety_failsafe` | `safety_arm()` | Transition to ARMED (fails if ESTOP) |
| `safety_failsafe` | `safety_disarm()` | Transition to DISARMED, calls e-stop |
| `safety_failsafe` | `safety_emergency_stop()` | Latch ESTOP state |
| `safety_failsafe` | `safety_estop_reset()` | Clear ESTOP → DISARMED (re-arm required) |
| `safety_failsafe` | `safety_is_armed()` | Query armed state |
| `safety_failsafe` | `safety_update_watchdog()` | Reset watchdog timer |
| `ps4` | `ps4_init(mac, callback)` | Start Bluepad32 + BTstack |
| `ps4` | `ps4_is_connected()` | Query gamepad connection |
| `controller_serial` | `controller_serial_init()` | Start UART + JSON parse task |
| `controller_http` | `controller_http_init()` | Start WiFi AP/STA + HTTP server |
| `controller_ps4` | `controller_ps4_init(mac)` | Register PS4 input callback |

---

## Boot Sequence (`main.c` → `app_main()`)

1. Initialize NVS (auto-erase on corruption via `init_nvs()`)
2. Read drive params from NVS `robot_cfg` (override Kconfig defaults)
3. Initialize safety system → DISARMED, boot LED pattern starts
4. Initialize motor control (pins, PWM channels, ramp task)
5. Initialize differential drive mixer (with NVS-overridden params)
6. Initialize control manager (50 Hz task, mutex)
7. Conditionally initialize: Serial controller, HTTP controller
8. Schedule PS4 init with 10 s delay (`ps4_init_task`) to let WiFi stabilize

---

## Guardrails and Best Practices

### DO:
- Always read existing code before modifying
- Test code compiles before committing
- Update PROGRESS.md when completing tasks
- Document design decisions in `/docs`
- Use Kconfig for all configuration
- Log all state changes and errors
- Handle all error cases explicitly
- Keep functions focused and single-purpose

### DON'T:
- ✗ Break CI workflows (they must always pass)
- ✗ Change folder structure without updating this file
- ✗ Hardcode values (use Kconfig or `#define`)
- ✗ Ignore compiler warnings
- ✗ Commit generated files (`sdkconfig`, `build/`)
- ✗ Skip error checking
- ✗ Enable Bluedroid or `esp_hidh` (conflicts with Bluepad32/BTstack)
- ✗ Remove or bypass safety features
- ✗ Change PWM frequency without updating both `FREQ_HZ` and `RESOLUTION` Kconfig values

---

## Documentation Policy

### When to Update

| Change | Required updates |
|--------|----------------|
| New control source | `docs/myname-protocol.md` + architecture.md |
| Architecture change | `docs/architecture.md` |
| CI/CD workflow change | `docs/cicd.md` |
| Milestone complete | `PROGRESS.md` |
| Any user-visible change | `README.md` |

### Doc Structure

Each doc file: purpose → context → details → examples → troubleshooting.

---

## Testing Checklist

Before marking a feature complete:

- [ ] Code compiles without warnings
- [ ] CI workflow passes
- [ ] Serial logs show expected behavior
- [ ] Safety features tested (e-stop, timeout, disarm)
- [ ] Configuration options validated
- [ ] Documentation updated
- [ ] PROGRESS.md updated

For hardware testing (when available):

- [ ] Tracks off ground initially
- [ ] PS4 controller pairs and connects (PS+Share → rapid light bar blink within ~10 s of boot)
- [ ] All buttons/sticks respond correctly
- [ ] Motors respond to throttle/steering
- [ ] E-stop immediately stops motors (latched — requires reboot)
- [ ] Failsafe triggers on timeout (~500 ms after controller disconnect)
- [ ] Slow mode limits speed correctly (hold L1)
- [ ] Serial/HTTP control work as alternatives

**Known hardware caveats** (v0.1.0):
- PS4 scan window is ~10 s after boot; miss it → must reboot
- No persistent BT pairing (re-pairs on every power cycle)
- No battery/current monitoring (future feature)

---

## Decision-Making Framework

When making a technical decision:

1. **Align with goals**: Does this serve a primary goal?
2. **Check non-goals**: Does this violate a non-goal?
3. **Consider safety**: Does this maintain or improve safety?
4. **Evaluate complexity**: Is this the simplest approach?
5. **Document**: Record the decision and rationale in `/docs` or `PROGRESS.md`

If uncertain: check this file → review existing code → consult `/docs` → add to PROGRESS.md under "Open Questions" → propose options with pros/cons. **Never guess or assume.**

---

## Summary

This repository is:
- **Structured**: Every file has a place and purpose
- **Automated**: CI/CD handles builds, releases, and deployments
- **Documented**: Every decision explained and justified
- **Safe**: Multiple layers of safety, conservative defaults
- **Maintainable**: Clear conventions, consistent style

Principle: **"Make it work, make it right, make it documented."**

---

*Last updated: 2026-05-17*
