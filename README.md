# Tracked Robot Firmware

ESP32-based firmware for a tank-style tracked robot controlled by a PS4 DualShock 4 controller, with Serial and HTTP fallback control.

## Features

- **PS4 Controller**: Wireless Bluetooth Classic HID — no third-party library needed
- **Multiple Control Modes**: PS4, Serial (UART), HTTP (Wi-Fi)
- **High-Performance PWM**: 20 kHz motor control for smooth, silent operation
- **Safety First**: Emergency stop, arming logic, 500 ms failsafe timeout
- **Differential Drive**: Tank-style steering with deadzone, expo curves, slow mode
- **Production Ready**: CI/CD builds, web-based flasher, comprehensive docs

## Hardware

### Components

| Part | Detail |
|------|--------|
| MCU | ESP32-WROOM-32 (4 MB flash) |
| Motors | 2× Topran 108 792 windshield wiper motors (12 V) |
| Drivers | 2× IBT-2 / BTS7960 43 A Dual H-Bridge modules |
| Controller | PS4 DualShock 4 (Bluetooth Classic HID) |
| Power | Milwaukee 12 V battery (4–6 Ah) |
| Buck converter | 12 V → 5 V for ESP32 logic |

### Wiring — ESP32 → IBT-2 Pin Mapping

| ESP32 GPIO | Signal | IBT-2 Left (Track A) | IBT-2 Right (Track B) |
|-----------|--------|----------------------|----------------------|
| 27 | RPWM | RPWM | — |
| 14 | LPWM | LPWM | — |
| 25 | R_EN | R_EN | — |
| 26 | L_EN | L_EN | — |
| 18 | RPWM | — | RPWM |
| 19 | LPWM | — | LPWM |
| 33 | R_EN | — | R_EN |
| 32 | L_EN | — | L_EN |
| 34 | R_IS | R_IS (sense, optional) | — |
| 35 | L_IS | L_IS (sense, optional) | — |
| 36 | R_IS | — | R_IS (sense, optional) |
| 39 | L_IS | — | L_IS (sense, optional) |
| 2 | LED | Status LED | — |

### Power Wiring

```
Milwaukee 12 V Battery
    ├── IBT-2 Left  B+  (12 V motor power)
    ├── IBT-2 Right B+  (12 V motor power)
    └── Buck Converter  (12 V → 5 V)
             ├── ESP32 VIN
             ├── IBT-2 Left  VCC
             └── IBT-2 Right VCC

★ CRITICAL — Common Ground:
    Battery GND ── ESP32 GND ── IBT-2 Left GND/B- ── IBT-2 Right GND/B-
```

> **⚠️ Safety**: Add a physical emergency-stop switch in series with the battery positive.
> The firmware e-stop is NOT a substitute for a hardware switch.

---

## Quick Start

### 1. Flash Firmware

#### Option A: Web Flasher (Easiest)

1. Open **https://joachimth.github.io/track-robot-v2/**
2. Click **Connect** and select your ESP32
3. Click **Flash Firmware** and wait for completion

*Requires Chrome or Edge browser.*

#### Option B: esptool (Manual)

```bash
pip install esptool

# Download release binaries from GitHub Releases, then:
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
  --before default_reset --after hard_reset write_flash \
  0x1000 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 track-robot.bin
```

#### Option C: Build from Source

```bash
# Install ESP-IDF v5.1.2
# https://docs.espressif.com/projects/esp-idf/en/v5.1.2/get-started/

git clone https://github.com/joachimth/track-robot-v2.git
cd track-robot-v2/firmware

idf.py menuconfig   # optional — review defaults
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

---

### 2. Pair PS4 Controller

Hold **PS + Share** together for ~3 seconds until the light bar flashes rapidly.

![PS4 pairing: hold PS and Share together](docs/images/ps4-pairing.png)

The ESP32 scans for ~10 seconds on boot. If pairing fails, reboot and try again.
After first pairing, press **PS** alone to reconnect on subsequent boots.

See [PS4 Setup Guide](docs/ps4-setup.md) for full instructions and troubleshooting.

---

### 3. Arm & Drive

> **⚠️ Motors are DISARMED at boot for safety.**

| PS4 Input | Action |
|-----------|--------|
| **Options** (≡) | **Arm** system |
| Left stick Y | Forward / Reverse throttle |
| Left stick X | Left / Right steering |
| **Cross** (✕) | **Emergency stop** (latched) |
| **L1** | Slow mode (50% speed, hold) |

Press **Options** to arm → drive with the left stick → press **Cross** for immediate stop.

---

### 4. Alternative Control Methods

#### Serial (UART) — 115200 baud 8N1

```json
{"throttle": 0.5, "steering": 0.0}
{"throttle": -0.3, "steering": 0.8}
{"estop": true}
{"arm": true}
```

Values: −1.0 to +1.0.

#### HTTP API (Wi-Fi)

Default: AP mode — SSID `TrackedRobot`, password `robot123`.

```bash
# Drive
curl -X POST http://192.168.4.1/control \
  -H "Content-Type: application/json" \
  -d '{"throttle": 0.5, "steering": 0.2}'

# Emergency stop
curl -X POST http://192.168.4.1/estop

# Arm
curl -X POST http://192.168.4.1/arm

# Status
curl http://192.168.4.1/status
```

Web UI: http://192.168.4.1/

---

## Configuration

Run `idf.py menuconfig` or edit `firmware/sdkconfig.defaults`:

| Setting | Menu path |
|---------|-----------|
| Motor GPIO pins | Robot Configuration → Motor Pins |
| PWM frequency | Robot Configuration → Motor Control |
| Control sources | Robot Configuration → Control Sources |
| Wi-Fi SSID / password | Robot Configuration → WiFi Configuration |
| Failsafe timeout | Robot Configuration → Safety |

---

## Safety Notes

1. **Test with tracks off the ground first**
2. **Always have a physical e-stop switch** on the battery line
3. **Start in slow mode** (hold L1) until familiar with response
4. **Verify common ground** before powering on
5. **IBT-2 drivers can supply 43 A** — ensure wiring is rated accordingly
6. **Firmware e-stop is not a substitute** for a hardware switch

---

## Documentation

| Doc | Description |
|-----|-------------|
| [Architecture](docs/architecture.md) | System design and data flow |
| [BTS7960 Wiring](docs/bts7960-wiring.md) | Full wiring diagrams |
| [PS4 Setup](docs/ps4-setup.md) | Controller pairing and button mapping |
| [Serial Protocol](docs/serial-protocol.md) | UART JSON command format |
| [HTTP API](docs/http-api.md) | REST endpoint reference |
| [Safety & Failsafe](docs/safety-failsafe.md) | Arming, e-stop, timeout |
| [PWM Tuning](docs/pwm-tuning.md) | Frequency and resolution guide |
| [CI/CD](docs/cicd.md) | Build and release pipeline |
| [Web Flasher](docs/web-flasher.md) | Browser-based flashing |

---

## Development

See [CLAUDE.md](CLAUDE.md) for coding guidelines and [PROGRESS.md](PROGRESS.md) for current status.

## License

MIT — see [LICENSE](LICENSE)

## Support

- Issues: https://github.com/joachimth/track-robot-v2/issues
- Discussions: https://github.com/joachimth/track-robot-v2/discussions
