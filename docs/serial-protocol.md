# Serial Protocol Specification

JSON-based serial control protocol for UART communication.

## Connection Parameters

- **Baud rate**: 115200 (configurable via Kconfig)
- **Data bits**: 8
- **Parity**: None
- **Stop bits**: 1
- **Flow control**: None

## Protocol Format

**Line-based JSON**: One command per line, terminated by `\n` or `\r\n`.

### Control Command

```json
{"throttle": <float>, "steering": <float>, "slow_mode": <bool>}
```

**Fields**:
- `throttle`: -1.0 to +1.0 (forward/reverse)
- `steering`: -1.0 to +1.0 (left/right)
- `slow_mode`: true/false (optional)

### Emergency Stop

```json
{"estop": true}
```

### Arm System

```json
{"arm": true}
```

## Examples

```bash
# Connect to ESP32 (Linux)
screen /dev/ttyUSB0 115200

# Full forward
{"throttle": 1.0, "steering": 0.0}

# Moderate reverse, turn left
{"throttle": -0.5, "steering": -0.3}

# Slow mode, gentle right turn
{"throttle": 0.3, "steering": 0.2, "slow_mode": true}

# Emergency stop
{"estop": true}

# Re-arm
{"arm": true}
```

## Python Example

```python
import serial
import json
import time

# Open serial port
ser = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)

def send_control(throttle, steering, slow_mode=False):
    cmd = {
        "throttle": throttle,
        "steering": steering,
        "slow_mode": slow_mode
    }
    ser.write((json.dumps(cmd) + '\n').encode())

def emergency_stop():
    ser.write(b'{"estop": true}\n')

def arm():
    ser.write(b'{"arm": true}\n')

# Arm system
arm()
time.sleep(0.5)

# Drive forward
send_control(0.5, 0.0)
time.sleep(2)

# Turn right
send_control(0.3, 0.5)
time.sleep(2)

# Stop
send_control(0.0, 0.0)

ser.close()
```

## Error Handling

Invalid JSON → Ignored, logged as warning.

## Latency

~20ms from command to motor response.

*Last updated: 2025-12-28*
