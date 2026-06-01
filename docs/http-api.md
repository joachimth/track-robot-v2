# HTTP API Specification

REST API for WiFi-based control and configuration.

## Base URL

| Mode | URL |
|------|-----|
| AP mode (always active) | `http://192.168.4.1` |
| STA mode (home WiFi) | Depends on DHCP — check serial monitor |

The ESP32 always runs the **TrackRobot-Setup** AP as a fallback, so `192.168.4.1`
is always reachable when connected to that network.

A **captive portal** is active on the AP: all DNS queries resolve to
`192.168.4.1` and any unknown HTTP path 302-redirects to the web UI, so most
phones/laptops pop the control page automatically on join. See
[captive-portal.md](captive-portal.md).

## Web UI

Navigate to `http://192.168.4.1/` in any browser.
The UI has four tabs: **Control**, **WiFi**, **Config**, **Status**.

## Endpoints

### POST /control

Send throttle and steering commands.

**Request**:
```json
{
  "throttle": <float -1.0 to +1.0>,
  "steering": <float -1.0 to +1.0>,
  "slow_mode": <bool>
}
```

**Response**: `{"status": "ok"}`

```bash
curl -X POST http://192.168.4.1/control \
  -H "Content-Type: application/json" \
  -d '{"throttle": 0.5, "steering": -0.2}'
```

---

### POST /arm

Arm the system (enable motors).

**Response**: `{"status": "armed"}`

```bash
curl -X POST http://192.168.4.1/arm
```

---

### POST /estop

Trigger emergency stop (latched — requires re-arm to clear).

**Response**: `{"status": "estop"}`

```bash
curl -X POST http://192.168.4.1/estop
```

---

### GET /status

Get current system status.

**Response**:
```json
{
  "state": "DISARMED",
  "armed": false,
  "source": "HTTP",
  "input": {
    "throttle": 0.0,
    "steering": 0.0,
    "slow_mode": false,
    "estop": false,
    "arm": false
  },
  "output": {
    "left_target": 0.0,
    "right_target": 0.0,
    "left_actual": 0.0,
    "right_actual": 0.0
  },
  "monitor": {
    "left_ma": 0,
    "right_ma": 0,
    "overcurrent": false,
    "battery_enabled": false,
    "battery_mv": 0,
    "battery_low": false
  },
  "wifi": {
    "ap": true,
    "sta_connected": false,
    "sta_connecting": false,
    "sta_ssid": "",
    "setup_ip": "192.168.4.1"
  }
}
```

**`state`**: `DISARMED`, `ARMED`, or `ESTOP`.
**`source`**: `NONE`, `PS4`, `SERIAL`, or `HTTP` (last active control source).

**`monitor`** block (see [Robot Monitoring](#robot-monitoring)):

| Field | Description |
|-------|-------------|
| `left_ma` / `right_ma` | Per-motor current draw in milliamps (max of the two IS pins) |
| `overcurrent` | `true` while a motor exceeds `CONFIG_ROBOT_OVERCURRENT_MA` |
| `battery_enabled` | `true` if a battery ADC pin is configured |
| `battery_mv` | Battery voltage in millivolts (0 when disabled) |
| `battery_low` | `true` when below `CONFIG_ROBOT_BATTERY_LOW_MV` |

All monitor fields read 0/false when monitoring is disabled or no ADC inputs resolve.

```bash
curl http://192.168.4.1/status
```

---

### POST /wifi

Save home WiFi credentials to NVS and attempt STA connection.
The setup AP stays active as fallback.

**Request**:
```json
{"ssid": "MyHomeNetwork", "password": "mypassword"}
```

**Response**: `{"status": "saved", "message": "WiFi saved, connecting now"}`

To clear saved credentials and revert to AP-only mode:
```json
{"ssid": "", "password": ""}
```

```bash
curl -X POST http://192.168.4.1/wifi \
  -H "Content-Type: application/json" \
  -d '{"ssid":"MyHomeNetwork","password":"secret"}'
```

---

### GET /config

Read current robot drive parameters (NVS overrides or Kconfig defaults).

**Response**:
```json
{
  "deadzone": 5,
  "expo": 30,
  "max_speed": 100,
  "slow_factor": 50,
  "note": "POST /config with same fields to update. Reboot to apply."
}
```

| Field | Range | Description |
|-------|-------|-------------|
| `deadzone` | 0–20 | Stick deadzone (percent) |
| `expo` | 0–100 | Expo curve factor (percent) |
| `max_speed` | 10–100 | Global speed limit (percent) |
| `slow_factor` | 10–100 | Slow-mode speed multiplier (percent) |

```bash
curl http://192.168.4.1/config
```

---

### POST /config

Save robot drive parameters to NVS. **Reboot required to apply.**
All fields are optional — only provided fields are updated.

**Request**:
```json
{
  "deadzone": 10,
  "expo": 20,
  "max_speed": 80,
  "slow_factor": 40
}
```

**Response**: `{"status": "saved", "message": "Config saved to NVS. Reboot to apply."}`

```bash
curl -X POST http://192.168.4.1/config \
  -H "Content-Type: application/json" \
  -d '{"max_speed": 80, "deadzone": 10}'
```

---

### POST /reboot

Reboot the ESP32 (applies saved NVS config changes).

**Response**: `{"status": "rebooting"}`

```bash
curl -X POST http://192.168.4.1/reboot
```

---

## JavaScript Example

```javascript
// Arm system
await fetch('http://192.168.4.1/arm', {method: 'POST'});

// Send control
await fetch('http://192.168.4.1/control', {
  method: 'POST',
  headers: {'Content-Type': 'application/json'},
  body: JSON.stringify({throttle: 0.5, steering: 0.2})
});

// Emergency stop
await fetch('http://192.168.4.1/estop', {method: 'POST'});

// Save home WiFi
await fetch('http://192.168.4.1/wifi', {
  method: 'POST',
  headers: {'Content-Type': 'application/json'},
  body: JSON.stringify({ssid: 'MyNetwork', password: 'secret'})
});

// Read config
const cfg = await (await fetch('http://192.168.4.1/config')).json();

// Update config (max speed 80%, then reboot)
await fetch('http://192.168.4.1/config', {
  method: 'POST',
  headers: {'Content-Type': 'application/json'},
  body: JSON.stringify({max_speed: 80})
});
await fetch('http://192.168.4.1/reboot', {method: 'POST'});
```

## NVS Config Storage

Robot drive parameters are stored in NVS namespace `robot_cfg` with keys
`deadzone`, `expo`, `max_speed`, `slow_factor`. Values fall back to
Kconfig defaults (`idf.py menuconfig → Robot Configuration → Differential Drive`)
if no NVS value is found.

## Robot Monitoring

The firmware samples the IBT-2 current-sense pins (and an optional battery
voltage divider) on ADC1 and surfaces the results in `GET /status` (the
`monitor` block) and the **Control** tab of the web UI. On over-current it
latches an emergency stop. Configure thresholds under
`idf.py menuconfig → Robot Configuration → Robot Monitoring`. Full hardware and
calibration details are in [docs/monitoring.md](monitoring.md).

## Latency

~50–100 ms from HTTP request to motor response (WiFi + processing).

*Last updated: 2026-06-01*
