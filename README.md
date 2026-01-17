# Tracked Robot Car Firmware

ESP32-based firmware for a tank-style tracked robot car controlled by PS3 controller, with Serial and HTTP control alternatives.

## Features

- **PS3 Controller Support**: Wireless Bluetooth control with full button/stick mapping
- **Multiple Control Modes**: PS3, Serial (UART), HTTP (Wi-Fi)
- **High-Performance PWM**: 20kHz motor control for smooth operation
- **Safety First**: Emergency stop, arming logic, failsafe timeout
- **Differential Drive**: Intuitive tank-style steering with expo curves
- **Production Ready**: Full CI/CD, web-based flasher, comprehensive docs

## Hardware

### Components

- **MCU**: ESP32 DevKitC with ESP32-WROVER-IE (8MB Flash, PSRAM)
- **Motors**: 2× Topran 108 792 windshield wiper motors (12V)
- **Drivers**: 2× BTS7960 43A Dual H-Bridge modules
- **Controller**: PS3 wireless controller (Bluetooth)
- **Power**: Milwaukee 12V battery (4-6Ah)
- **Buck Converter**: 12V → 5V for ESP32 logic

### Wiring

#### ESP32 → BTS7960 Pin Mapping

| Function | ESP32 GPIO | BTS7960 Left | BTS7960 Right |
|----------|------------|--------------|---------------|
| RPWM | GPIO 25 | RPWM | — |
| LPWM | GPIO 26 | LPWM | — |
| R_EN | GPIO 32 | R_EN | — |
| L_EN | GPIO 33 | L_EN | — |
| RPWM | GPIO 27 | — | RPWM |
| LPWM | GPIO 14 | — | LPWM |
| R_EN | GPIO 12 | — | R_EN |
| L_EN | GPIO 13 | — | L_EN |
| Status LED | GPIO 2 | — | — |

#### Power Wiring

```
Milwaukee 12V Battery
    ├── BTS7960 Left (12V power)
    ├── BTS7960 Right (12V power)
    ├── Buck Converter Input (12V)
    │   └── Buck Converter Output (5V)
    │       ├── ESP32 VIN (5V)
    │       └── BTS7960 Logic Power (5V)
    └── Common Ground ★ CRITICAL ★
        (Battery GND, ESP32 GND, both BTS7960 GND)
```

**⚠️ GROUNDING**: All grounds MUST be connected together:
- Battery negative
- ESP32 GND
- Both BTS7960 GND pins
- Buck converter GND

**⚠️ SAFETY**: Add a physical emergency stop switch in series with battery for safety!

## Quick Start

### 1. Flash Firmware

#### Option A: Web Flasher (Easiest)

1. Visit: **https://joachimth.github.io/track-robot-v2/**
2. Click "Connect" and select your ESP32
3. Click "Flash Firmware"
4. Wait for completion

*Requires Chrome/Edge browser*

#### Option B: esptool (Manual)

```bash
# Install esptool
pip install esptool

# Download latest release binaries from GitHub Releases
# Then flash:
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
  --before default_reset --after hard_reset write_flash \
  0x1000 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 track-robot.bin
```

#### Option C: Build from Source

```bash
# Install ESP-IDF v5.1.2
# See: https://docs.espressif.com/projects/esp-idf/en/v5.1.2/get-started/

# Clone with submodules
git clone --recursive https://github.com/joachimth/track-robot-v2.git
cd track-robot-v2/firmware

# Configure (optional)
idf.py menuconfig

# Build and flash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### 2. Pair PS3 Controller

The ESP32 needs the PS3 controller's Bluetooth MAC address:

#### Get Controller MAC

**Linux/Mac:**
```bash
# Install sixpair
git clone https://github.com/user-none/sixpair.git
cd sixpair
make

# Connect controller via USB, then:
sudo ./sixpair
# Note the "Current Bluetooth master" address
```

**Windows:** Use SCP Toolkit or check controller properties

#### Set MAC in Firmware

Edit `firmware/main/main.c`:
```c
// Replace with your controller's MAC address
uint8_t ps3_mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
```

Rebuild and flash.

#### Connect

1. Press PS button on controller
2. Controller should connect (LEDs stabilize)
3. Check serial monitor for "PS3 connected" message

### 3. Arming & Control

**⚠️ IMPORTANT**: Motors are DISARMED at boot for safety!

**To Arm:**
- Press **START** button on PS3 controller

**To Drive:**
- **Left Stick Y-axis**: Forward/Backward throttle
- **Right Stick X-axis**: Left/Right steering
- Combine for tank-style driving

**Emergency Stop:**
- Press **X** button → latches e-stop
- Press **START** to re-arm

**Slow Mode:**
- Press **Triangle** to toggle (50% speed limit)

### 4. Alternative Control Methods

#### Serial Control (UART)

Connect to ESP32 UART (115200 baud, 8N1):

```json
{"throttle": 0.5, "steering": 0.0}
{"throttle": -0.3, "steering": 0.8}
{"estop": true}
{"arm": true}
```

Values: -1.0 to +1.0

#### HTTP API (Wi-Fi)

Default: AP mode, SSID `TrackedRobot`, password `robot123`

**Endpoints:**

```bash
# Control
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

## Configuration

Edit `firmware/sdkconfig.defaults` or run `idf.py menuconfig`:

- **Motor pins**: `Component config → Robot Config → Motor Pins`
- **PWM settings**: `Component config → Robot Config → Motor Control`
- **Control sources**: `Component config → Robot Config → Control Sources`
- **Wi-Fi**: `Component config → Robot Config → WiFi Configuration`

## Safety Notes

⚠️ **CRITICAL SAFETY WARNINGS**:

1. **Test with tracks OFF the ground** initially
2. **Always have physical emergency stop** on battery line
3. **Start in slow mode** until familiar with response
4. **Check all wiring** before powering on
5. **Motors are powerful** (43A drivers, 12V motors)
6. **Firmware e-stop is NOT a substitute** for physical switch
7. **Ensure proper grounding** to prevent damage

## Documentation

- [Architecture Overview](docs/architecture.md)
- [PWM Frequency Guide](docs/pwm-tuning.md)
- [BTS7960 Wiring](docs/bts7960-wiring.md)
- [PS3 Setup](docs/ps3-setup.md)
- [Serial Protocol](docs/serial-protocol.md)
- [HTTP API](docs/http-api.md)
- [Safety & Failsafe](docs/safety-failsafe.md)
- [CI/CD & Releases](docs/cicd.md)
- [Web Flasher](docs/web-flasher.md)

## Development

See [CLAUDE.md](CLAUDE.md) for development guidelines and [PROGRESS.md](PROGRESS.md) for current status.

## License

MIT License - see LICENSE file

## Support

- Issues: https://github.com/joachimth/track-robot-v2/issues
- Discussions: https://github.com/joachimth/track-robot-v2/discussions
