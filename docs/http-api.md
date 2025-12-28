# HTTP API Specification

REST API for WiFi-based control.

## Base URL

- **AP Mode (default)**: `http://192.168.4.1`
- **STA Mode**: Depends on DHCP (check serial monitor)

## Endpoints

### POST /control

Send throttle and steering commands.

**Request**:
```json
{
  "throttle": <float>,
  "steering": <float>,
  "slow_mode": <bool>
}
```

**Response**:
```json
{"status": "ok"}
```

**Example**:
```bash
curl -X POST http://192.168.4.1/control \
  -H "Content-Type: application/json" \
  -d '{"throttle": 0.5, "steering": -0.2}'
```

### POST /estop

Trigger emergency stop.

**Request**: Empty body

**Response**:
```json
{"status": "estop"}
```

**Example**:
```bash
curl -X POST http://192.168.4.1/estop
```

### POST /arm

Arm the system (enable motors).

**Request**: Empty body

**Response**:
```json
{"status": "armed"}
```

**Example**:
```bash
curl -X POST http://192.168.4.1/arm
```

### GET /status

Get current system status.

**Response**:
```json
{
  "armed": <bool>,
  "source": <int>
}
```

**Source codes**:
- 0: None
- 1: PS3
- 2: Serial
- 3: HTTP

**Example**:
```bash
curl http://192.168.4.1/status
# {"armed":true,"source":3}
```

### GET /

Web UI for manual control.

**Response**: HTML page with control sliders.

## Web UI

Navigate to `http://192.168.4.1/` in browser.

**Features**:
- ARM / E-STOP buttons
- Throttle slider (-100 to +100)
- Steering slider (-100 to +100)
- Send button

## JavaScript Example

```javascript
// Arm system
fetch('http://192.168.4.1/arm', {method: 'POST'});

// Send control
fetch('http://192.168.4.1/control', {
  method: 'POST',
  headers: {'Content-Type': 'application/json'},
  body: JSON.stringify({throttle: 0.5, steering: 0.2})
});

// Emergency stop
fetch('http://192.168.4.1/estop', {method: 'POST'});

// Get status
fetch('http://192.168.4.1/status')
  .then(r => r.json())
  .then(data => console.log(data));
```

## Latency

~100ms from HTTP request to motor response (WiFi + processing).

*Last updated: 2025-12-28*
