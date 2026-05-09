# HTTP API Specification

REST API for WiFi-based control and configuration.

## Base URL

| Mode | URL |
|------|-----|
| AP mode (always active) | `http://192.168.4.1` |
| STA mode (home WiFi) | Depends on DHCP — check serial monitor |

The ESP32 always runs the **TrackRobot-Setup** AP as a fallback, so `192.168.4.1`
is always reachable when connected to that network.

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
  "armed": true,
  "source": 3,
  "wifi": {
    "ap": true,
    "sta_connected": false,
    "sta_connecting": false,
    "sta_ssid": "",
    "setup_ip": "192.168.4.1"
  }
}
```

**Source codes**: 0 = None, 1 = PS4, 2 = Serial, 3 = HTTP

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

## Latency

~50–100 ms from HTTP request to motor response (WiFi + processing).

*Last updated: 2026-05-09*
