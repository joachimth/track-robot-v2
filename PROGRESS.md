# PROGRESS.md  Project Status and Roadmap

Track the current state, completed work, next steps, and open questions for the tracked robot firmware project.

---

## Current Status: =§ **Initial Development**

**Version**: 0.1.0 (pre-release)
**Last Updated**: 2025-12-28

---

##  Completed Tasks

### Project Structure
- [x] Repository initialized with MIT license
- [x] Directory structure created (firmware/, docs/, web-flasher/, .github/)
- [x] README.md with comprehensive quick start guide
- [x] CLAUDE.md with development guidelines
- [x] PROGRESS.md for status tracking

### Documentation (Planned)
- [ ] Architecture overview
- [ ] PWM tuning guide
- [ ] BTS7960 wiring guide
- [ ] PS3 setup guide
- [ ] Serial protocol specification
- [ ] HTTP API specification
- [ ] Safety and failsafe documentation
- [ ] CI/CD workflow documentation
- [ ] Web flasher architecture

### Firmware Core
- [ ] ESP-IDF project structure
- [ ] Kconfig configuration system
- [ ] Main application entry point
- [ ] PS3 component integration (submodule)

### Motor Control
- [ ] LEDC PWM driver (20kHz @ 12-bit)
- [ ] BTS7960 driver implementation
- [ ] Motor control API (set_speeds, e-stop)
- [ ] Ramping/slew rate limiting

### Motion Control
- [ ] Differential drive mixer
- [ ] Deadzone handling
- [ ] Expo curve implementation
- [ ] Slow mode support

### Control Sources
- [ ] PS3 controller integration
- [ ] Serial (UART) control
- [ ] HTTP (Wi-Fi) control
- [ ] Control arbitration manager

### Safety
- [ ] Arming state machine
- [ ] Emergency stop (latched)
- [ ] Failsafe timeout (500ms)
- [ ] Boot disarmed state
- [ ] Status LED patterns

### CI/CD
- [ ] Firmware build workflow (ci.yml)
- [ ] Release workflow (release.yml)
- [ ] GitHub Pages deployment (pages.yml)

### Web Flasher
- [ ] HTML/CSS/JS implementation
- [ ] Web Serial API integration
- [ ] Firmware manifest support
- [ ] User instructions

---

## <¯ Next Steps (Prioritized)

### Phase 1: Core Firmware Foundation
1. **Create ESP-IDF project structure**
   - CMakeLists.txt (root and main/)
   - sdkconfig.defaults
   - partitions.csv (8MB flash)
   - main.c skeleton

2. **Implement motor control layer**
   - pwm_ledc.c (LEDC peripheral driver)
   - motor_bts7960.c (BTS7960 interface)
   - Unit test with simple PWM output

3. **Implement differential drive mixer**
   - mixer_diffdrive.c
   - Deadzone, expo, clamping logic
   - Unit test with various inputs

4. **Implement safety layer**
   - safety_failsafe.c
   - Arming state, e-stop, timeout
   - Status LED patterns

### Phase 2: Control Sources
5. **Integrate PS3 controller**
   - Add joachimth/esp32-ps3 as submodule
   - controller_ps3.c wrapper
   - Test pairing and connection

6. **Implement control manager**
   - control_manager.c (arbitration logic)
   - Source priority and timeout handling
   - Integration with safety layer

7. **Implement serial control**
   - controller_serial.c
   - JSON parsing (cJSON)
   - Protocol validation

8. **Implement HTTP control**
   - controller_http.c
   - ESP HTTP server
   - REST API endpoints
   - Basic web UI

### Phase 3: Testing and Documentation
9. **Complete all documentation**
   - Write all /docs/*.md files
   - Add wiring diagrams
   - Troubleshooting guides

10. **Implement CI/CD workflows**
    - ci.yml (build on push/PR)
    - release.yml (build + release on tag)
    - pages.yml (deploy web flasher)

11. **Create web flasher**
    - index.html with esp-web-tools style
    - Manifest generation in CI
    - Test deployment to GitHub Pages

### Phase 4: Hardware Testing
12. **Bench testing (tracks off ground)**
    - PS3 controller pairing
    - Motor response to inputs
    - E-stop functionality
    - Failsafe timeout

13. **Field testing (tracks on ground)**
    - Driving characteristics
    - PWM frequency tuning
    - Battery runtime
    - Thermal performance

14. **Release v1.0.0**
    - Tag and release
    - Binary artifacts
    - Web flasher live

---

## =' Currently Working On

- **Initial repository bootstrap**
  - Creating all project files
  - Setting up firmware structure
  - Writing documentation

---

## S Open Questions

### Hardware
- **Q**: Are the GPIO pins finalized, or do we need flexibility?
  - **A**: Pins are configurable via Kconfig, defaults proposed and documented

- **Q**: What is the actual motor no-load current and stall current?
  - **A**: TBD  measure with multimeter during bench testing

- **Q**: Does the buck converter provide sufficient current for ESP32 + BTS7960 logic?
  - **A**: TBD  verify during power-up testing (ESP32 ~500mA, BTS7960 logic minimal)

### Firmware
- **Q**: Should we support OTA (over-the-air) updates?
  - **A**: Not in v1.0 scope (use web flasher or esptool), consider for v2.0

- **Q**: Should we log telemetry to SD card?
  - **A**: Not in v1.0 scope, Serial logging sufficient for debugging

- **Q**: Do we need configurable PID control for motor speed?
  - **A**: Not initially  open-loop PWM is sufficient for DC motors with encoders

### Control
- **Q**: Should we support multiple simultaneous PS3 controllers?
  - **A**: Not in v1.0 scope  single controller only

- **Q**: Should HTTP control have authentication?
  - **A**: Not in v1.0 (AP mode, physical access assumed), add in v2.0 if needed

### Safety
- **Q**: Should we monitor battery voltage and auto-stop on low voltage?
  - **A**: Nice-to-have for v2.0  add ADC monitoring for battery voltage

- **Q**: Should we have a "limp mode" for degraded operation?
  - **A**: Not in v1.0  fail-safe is full stop, not reduced functionality

---

## =Ë Test Checklist (For Hardware Validation)

### Pre-Power Tests
- [ ] Visual inspection of all wiring
- [ ] Continuity test: GND connections (battery, ESP32, BTS7960s)
- [ ] Voltage test: Buck converter output is 5V ± 0.25V
- [ ] Isolation test: No shorts between power rails

### Power-Up Tests
- [ ] ESP32 boots and shows serial output
- [ ] Wi-Fi AP mode activates (SSID visible)
- [ ] Status LED shows boot pattern

### Motor Tests (Tracks OFF Ground)
- [ ] Motors are DISARMED at boot (no movement on inputs)
- [ ] Arming via PS3 START button works
- [ ] Throttle stick controls both tracks forward/backward
- [ ] Steering stick creates differential (left/right turns)
- [ ] E-stop (X button) immediately stops motors
- [ ] Re-arm (START button) after e-stop works
- [ ] Slow mode (Triangle button) limits speed to 50%
- [ ] Failsafe timeout stops motors after 500ms of no input

### Control Source Tests
- [ ] PS3 controller pairs and connects reliably
- [ ] Serial control (JSON commands) works
- [ ] HTTP API responds to POST /control
- [ ] HTTP API responds to POST /estop
- [ ] HTTP API responds to POST /arm
- [ ] HTTP API responds to GET /status
- [ ] Web UI loads and controls robot

### Driving Tests (Tracks ON Ground)
- [ ] Forward/backward motion is smooth
- [ ] Left/right turns work correctly (differential speeds)
- [ ] Zero-radius turn (opposite track directions) works
- [ ] No audible motor whine (20kHz PWM is silent)
- [ ] Motors do not overheat (< 60°C after 5 min run)
- [ ] Battery voltage stays above 10V under load

### Safety Tests
- [ ] E-stop activates within 100ms of button press
- [ ] Failsafe triggers within 600ms of controller disconnect
- [ ] Physical e-stop switch cuts all power
- [ ] Robot recovers gracefully from power cycle

---

## =€ Release Checklist

Before tagging a release:

- [ ] All code compiles without warnings
- [ ] CI workflows pass (ci.yml)
- [ ] All documentation complete and accurate
- [ ] Hardware test checklist completed
- [ ] Web flasher tested end-to-end
- [ ] CHANGELOG.md updated
- [ ] README.md updated with release notes
- [ ] Version number bumped in manifest

**Release process**:
```bash
# Update version in files
# Commit changes
git add .
git commit -m "Release v1.0.0"

# Create annotated tag
git tag -a v1.0.0 -m "Release v1.0.0: Initial production release"

# Push commits and tag
git push origin main
git push origin v1.0.0

# CI will automatically:
# - Build firmware
# - Create GitHub Release
# - Upload binaries
# - Generate manifest.json
# - Deploy web flasher
```

---

## = Known Issues

### None yet (project just started)

*Issues will be tracked here as they are discovered during development and testing.*

---

## =¡ Future Enhancements (v2.0+)

### Motor Control
- Closed-loop speed control (if encoders added)
- Current sensing and limiting
- Regenerative braking (if BTS7960 supports)

### Control
- Multiple PS3 controller support
- RC receiver support (PWM/PPM/SBUS)
- Autonomous modes (waypoint navigation)
- Companion computer integration (ROS/ROS2)

### Safety
- Battery voltage monitoring with low-voltage cutoff
- Motor current monitoring
- Tilt sensor (auto-stop if flipped)
- Geofencing (GPS-based)

### Features
- OTA firmware updates
- SD card logging (telemetry, diagnostics)
- OLED display (status, battery, runtime)
- WS2812 LED strips (status, effects)

### Infrastructure
- Unit tests (ESP-IDF test framework)
- Integration tests (simulated hardware)
- Code coverage reporting
- Automated hardware-in-the-loop testing

---

## =Ê Metrics

### Code Stats (Target)
- **Firmware**: ~3000-5000 lines of C code
- **Documentation**: ~10 markdown files, ~5000 words
- **Tests**: TBD (add in v2.0)

### Performance Targets
- **Boot time**: < 3 seconds to ready state
- **PS3 latency**: < 50ms from button press to motor response
- **Serial latency**: < 20ms from command to motor response
- **HTTP latency**: < 100ms from API call to motor response
- **Failsafe trigger**: < 600ms from timeout to motors stopped

### Reliability Targets
- **Uptime**: > 99% (no crashes during normal operation)
- **PS3 reconnect**: < 5 seconds after disconnect
- **E-stop reliability**: 100% (must always work)
- **Failsafe reliability**: 100% (must always trigger on timeout)

---

## =Ý Notes

### Design Decisions Made
1. **PWM frequency: 20kHz @ 12-bit resolution**
   - Rationale: Silent operation, excellent resolution, within BTS7960 limits
   - Documented in: docs/pwm-tuning.md

2. **Control arbitration: "Owner lock" model**
   - Rationale: Simple, predictable, no priority conflicts
   - Documented in: docs/architecture.md

3. **Boot state: DISARMED**
   - Rationale: Safety  never move unexpectedly on power-up
   - Documented in: docs/safety-failsafe.md

4. **Failsafe timeout: 500ms**
   - Rationale: Fast enough to stop quickly, slow enough to avoid false triggers
   - Documented in: docs/safety-failsafe.md

5. **PS3 component: joachimth/esp32-ps3 fork**
   - Rationale: Maintained, tested, ESP-IDF component structure
   - Documented in: docs/ps3-setup.md

### Decisions Pending
- Final GPIO pin mapping (waiting for hardware availability)
- BTS7960 enable pin behavior (always high, or PWM gated?)
- Serial protocol format (JSON vs binary framing)
- HTTP authentication (none vs basic auth vs token)

---

## = References

### ESP32
- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/v5.1.2/)
- [ESP32-WROVER-IE Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-wrover-e_esp32-wrover-ie_datasheet_en.pdf)
- [LEDC PWM Documentation](https://docs.espressif.com/projects/esp-idf/en/v5.1.2/esp32/api-reference/peripherals/ledc.html)

### Hardware
- [BTS7960 Datasheet](https://www.infineon.com/dgdl/Infineon-BTS7960-DS-v01_00-EN.pdf?fileId=db3a30433fa9412f013fbe32289b7c17)
- [Topran 108 792 Motor Specs](https://www.topran.de/)

### PS3 Controller
- [joachimth/esp32-ps3 GitHub](https://github.com/joachimth/esp32-ps3)
- [PS3 Controller Bluetooth Protocol](https://www.psdevwiki.com/ps3/DualShock_3)

### Web Flasher
- [Web Serial API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API)
- [ESP Web Tools](https://github.com/esphome/esp-web-tools)

---

*Last updated: 2025-12-28*
*Next review: After Phase 1 completion*
