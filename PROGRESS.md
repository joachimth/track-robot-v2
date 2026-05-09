# PROGRESS.md — Project Status

---

## Current Status: 🔧 **CI Build Passing — Awaiting Hardware Test**

**Version**: 0.1.0 (pre-release)
**Last Updated**: 2026-05-09

---

## ✅ Completed

### Hardware Target
- [x] Switched from ESP32-WROVER-IE to **ESP32-WROOM-32** (4 MB flash, no PSRAM)
- [x] Updated partition table for 4 MB flash
- [x] All motor GPIO pins set to actual wiring (see below)

### Firmware Core
- [x] ESP-IDF v5.1.2 project structure (CMakeLists, Kconfig, partitions)
- [x] `main.c` initialises all subsystems in correct order
- [x] `control_frame.h` — standardised control frame interface
- [x] `control_manager.c` — 50 Hz arbitration loop with watchdog timeout

### Motor Control
- [x] `pwm_ledc.c` — 20 kHz / 12-bit LEDC PWM driver
- [x] `motor_bts7960.c` — dual IBT-2 driver (direction, ramp, e-stop)
- [x] Slew-rate limiter (200 ms ramp, configurable)
- [x] Per-motor direction inversion via Kconfig

### Motion Control
- [x] `mixer_diffdrive.c` — tank-style differential drive
- [x] Deadzone, expo curve, max speed, slow mode factor

### Safety
- [x] `safety_failsafe.c` — DISARMED / ARMED / ESTOP state machine
- [x] Boot disarmed (motors never move on power-up)
- [x] 500 ms watchdog timeout
- [x] Latched e-stop with re-arm

### Control Sources
- [x] **PS4 DualShock 4** via Bluepad32 + BTstack
  - Auto-scan + auto-connect; supports PS4, PS5, Xbox, Switch controllers
  - Left stick Y/X → throttle/steering
  - Options → arm, Cross → e-stop, L1 → slow mode
- [x] **Serial (UART)** — JSON line protocol
- [x] **HTTP (Wi-Fi)** — REST API + tab-based web UI (AP mode default)
  - Tab UI: Control / WiFi / Config / Status tabs
  - AP always active (TrackRobot-Setup / trackrobot / 192.168.4.1)
  - Optional STA home WiFi (saved to NVS via `/wifi`)
  - Drive config via `/config` GET/POST (NVS-backed, reboot to apply)
  - Reboot via `/reboot` POST

### CI / CD
- [x] `ci.yml` — build on every push and PR (ESP-IDF v5.1.2, ESP32 target)
- [x] `release.yml` — build + GitHub Release on `v*.*.*` tags
- [x] `pages.yml` — deploy web flasher to GitHub Pages
- [x] Node.js 24 (`FORCE_JAVASCRIPT_ACTIONS_TO_NODE24`) — no deprecation warnings

### Documentation
- [x] `README.md` — up-to-date quick start with correct hardware
- [x] `docs/architecture.md` — system design and data flow
- [x] `docs/bts7960-wiring.md` — correct IBT-2 pin mapping
- [x] `docs/ps4-setup.md` — pairing guide with image
- [x] `docs/safety-failsafe.md` — state machine and test procedures
- [x] `docs/ps3-setup.md` — **deleted** (PS3 support removed)

---

## Pin Mapping (Confirmed)

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

## 🚧 Next Steps

### Immediate
1. **Hardware test** — flash v0.1.0 and test PS4 pairing on actual hardware
2. **Verify motor directions** — confirm RPWM = reverse per IBT-2 datasheet;
   swap wiring or toggle `invert_*` Kconfig if needed
3. **Verify PS4 HID report offsets** — confirm button/axis byte positions
   match `ps4.c` constants by logging raw report data during first test

### Phase 2 (After Initial Hardware Validation)
4. Release **v1.0.0** tag → triggers CI build + GitHub Release
5. Test web flasher end-to-end
6. Add current-sense ADC monitoring (overcurrent detection)
7. Add battery voltage monitoring

---

## Open Questions

| Question | Status |
|---------|--------|
| IBT-2 RPWM — is it reverse or forward? | Confirm on hardware; Kconfig `invert_*` as fallback |
| PS4 HID report byte layout | Matches DS4 spec — verify with raw log on first connect |
| Buck converter current headroom at full load | Measure during bench test |
| Motor no-load / stall current | Measure during bench test |

---

## Known Issues

| Issue | Severity | Workaround |
|-------|---------|-----------|
| PS4 scan window is ~10 s — miss it and you must reboot | Low | Reboot ESP32; controller reconnects automatically after first pair |
| No persistent BT pairing storage | Low | Controller re-pairs on power cycle; first scan required each boot |
| No battery voltage monitoring | Low | Not in v0.1 scope |

---

## Release Checklist (for v1.0.0)

- [ ] Hardware test: motors respond correctly
- [ ] Hardware test: PS4 pairs and reconnects reliably
- [ ] Hardware test: e-stop and failsafe timeout verified
- [ ] CI passes on main branch
- [ ] All docs up to date
- [ ] Web flasher tested end-to-end

```bash
git tag -a v1.0.0 -m "Release v1.0.0"
git push origin v1.0.0
# CI builds firmware, creates release, deploys web flasher automatically
```

---

*Last updated: 2026-05-09*
