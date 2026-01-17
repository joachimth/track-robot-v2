# Architecture Overview

This document describes the high-level architecture of the tracked robot firmware.

## System Overview

```
┌─────────────────────────────────────────────────────────────┐
│                     ESP32-WROVER-IE                          │
│                                                              │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐           │
│  │    PS3     │  │   Serial   │  │    HTTP    │           │
│  │ Controller │  │ Controller │  │ Controller │           │
│  └─────┬──────┘  └─────┬──────┘  └─────┬──────┘           │
│        │               │               │                   │
│        └───────────────┴───────────────┘                   │
│                        │                                    │
│                 ┌──────▼──────┐                            │
│                 │   Control   │                            │
│                 │   Manager   │ (Arbitration)             │
│                 └──────┬──────┘                            │
│                        │                                    │
│                 ┌──────▼──────┐                            │
│                 │   Safety    │                            │
│                 │  & Failsafe │                            │
│                 └──────┬──────┘                            │
│                        │                                    │
│                 ┌──────▼──────┐                            │
│                 │ Differential│                            │
│                 │    Mixer    │                            │
│                 └──────┬──────┘                            │
│                        │                                    │
│                 ┌──────▼──────┐                            │
│                 │    Motor    │                            │
│                 │   Control   │                            │
│                 └──────┬──────┘                            │
│                        │                                    │
└────────────────────────┼────────────────────────────────────┘
                         │
         ┌───────────────┴───────────────┐
         │                               │
    ┌────▼────┐                     ┌────▼────┐
    │BTS7960  │                     │BTS7960  │
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

Three independent control sources, all producing standardized `control_frame_t`:

#### PS3 Controller (`controller_ps3.c`)
- **Interface**: Bluetooth Classic (BR/EDR)
- **Library**: joachimth/esp32-ps3
- **Inputs**: Analog sticks, buttons
- **Mapping**:
  - Left stick Y → throttle
  - Right stick X → steering
  - X button → emergency stop
  - START button → arm/disarm
  - Triangle → toggle slow mode

#### Serial Controller (`controller_serial.c`)
- **Interface**: UART (default 115200 baud)
- **Protocol**: JSON lines
- **Example**: `{"throttle": 0.5, "steering": -0.2}`
- **Use case**: External microcontroller, PC control

#### HTTP Controller (`controller_http.c`)
- **Interface**: WiFi (AP or STA mode)
- **Protocol**: REST API + simple web UI
- **Endpoints**:
  - `POST /control` - Send throttle/steering
  - `POST /estop` - Emergency stop
  - `POST /arm` - Arm system
  - `GET /status` - Get current state
  - `GET /` - Web UI

### 2. Control Manager (`control_manager.c`)

**Arbitration Logic**: "Owner Lock" model

- Last active source takes control
- Source remains owner until timeout (500ms default)
- Timeout → safe stop (motors off)
- No priority system (all sources equal)

**Control Loop**:
- Runs at 50Hz (20ms period)
- Checks timeout
- Handles arming/disarming
- Passes control frame to mixer
- Updates safety watchdog

### 3. Safety & Failsafe (`safety_failsafe.c`)

**States**:
- `DISARMED` - Motors disabled (boot default)
- `ARMED` - Motors enabled
- `ESTOP` - Emergency stop (latched)

**Safety Features**:
- Boot disarmed (never move unexpectedly)
- Failsafe timeout (auto-disarm on input loss)
- Emergency stop (immediate motor stop)
- Re-arm required after e-stop
- Status LED patterns

**Failsafe Behavior**:
1. Control input stops
2. Timeout timer starts (500ms)
3. Timeout expires → auto-disarm
4. Motors stop via `motor_emergency_stop()`

### 4. Differential Drive Mixer (`mixer_diffdrive.c`)

Converts `(throttle, steering)` → `(left_speed, right_speed)`

**Algorithm**:
```
1. Apply deadzone (ignore small inputs near zero)
2. Apply expo curve (finer control near center)
3. Mix: left = throttle + steering
        right = throttle - steering
4. Clamp to [-1.0, +1.0]
5. Apply max speed limit
6. Apply slow mode factor (if enabled)
```

**Configuration**:
- `deadzone`: 0-20% (default 5%)
- `expo`: 0-100% (default 30%)
- `max_speed`: 10-100% (default 100%)
- `slow_mode_factor`: 10-100% (default 50%)

### 5. Motor Control (`motor_bts7960.c`)

**Features**:
- Dual BTS7960 H-bridge drivers
- 20kHz PWM @ 12-bit resolution
- Ramping/slew rate limiting (200ms default)
- Per-motor direction inversion
- Emergency stop (bypass ramping)

**PWM Channels**:
- Left motor: RPWM (reverse), LPWM (forward)
- Right motor: RPWM (reverse), LPWM (forward)
- Enable pins: R_EN, L_EN (always high)

**Ramping**:
- Limits acceleration to prevent current spikes
- Configurable ramp rate (ms per 100% change)
- Emergency stop bypasses ramping

### 6. PWM Driver (`pwm_ledc.c`)

**ESP32 LEDC Peripheral**:
- Low-speed mode (more channels)
- Timer 0 shared by all channels
- 4 channels total (2 per motor)
- Configurable frequency and resolution

**Default Settings**:
- Frequency: 20kHz
- Resolution: 12-bit (4096 steps)
- Actual frequency: 80MHz / 4096 = 19.53kHz

## Data Flow

### Control Frame Flow

```
PS3/Serial/HTTP → control_frame_t → Control Manager
                                            ↓
                                     Safety Check
                                            ↓
                                  Differential Mixer
                                            ↓
                                (left_speed, right_speed)
                                            ↓
                                      Motor Control
                                            ↓
                                        PWM Output
                                            ↓
                                      BTS7960 Drivers
                                            ↓
                                         Motors
```

### Safety Integration

Every control loop iteration:
1. Check if armed (if not, motors = 0)
2. Check timeout (if expired, disarm)
3. Check e-stop (if active, motors = 0)
4. Update watchdog (reset timeout timer)
5. Apply control (only if armed and not e-stopped)

## Configuration System

All configuration via Kconfig (`main/Kconfig.projbuild`):
- Motor GPIO pins
- PWM frequency and resolution
- Deadzone, expo, max speed
- Failsafe timeout
- WiFi settings
- Enable/disable control sources

Defaults in `sdkconfig.defaults`.

## FreeRTOS Tasks

| Task | Priority | Stack | Purpose |
|------|----------|-------|---------|
| `control_task` | 5 | 4KB | Main control loop (50Hz) |
| `serial_task` | 4 | 4KB | Serial input processing |
| `motor_ramp_task` | 4 | 2KB | Motor ramping (50Hz) |
| `watchdog_task` | 5 | 2KB | Failsafe timeout monitor |
| `led_task` | 3 | 2KB | Status LED patterns |

PS3 library manages its own tasks internally.

## Design Decisions

### Why "Owner Lock" Arbitration?

**Alternatives considered**:
- Fixed priority (PS3 > Serial > HTTP)
- Voting/averaging
- Manual source selection

**Chosen**: Owner lock

**Rationale**:
- Simple and predictable
- No conflicts or priority wars
- Natural UX (last input wins)
- Easy to debug (clear active source)

### Why 20kHz PWM?

- Above audible range (no motor whine)
- BTS7960 rated to ~25kHz
- 12-bit resolution = 0.024% steps (very smooth)
- Trade-off: Higher freq = lower resolution
- 19.53kHz actual (80MHz / 4096)

### Why Boot Disarmed?

**Safety first**: Never move unexpectedly on power-up.

Alternatives considered:
- Auto-arm on PS3 connect: Too dangerous
- Config option: Adds complexity
- Manual arm: Simple, safe, predictable ✓

## Testing Strategy

### Unit Testing (Future)
- Mixer math validation
- Deadzone/expo curves
- Clamping logic

### Integration Testing
- Control arbitration switching
- Failsafe timeout behavior
- E-stop latching

### Hardware Testing
- Motor response to inputs
- PWM frequency measurement
- Failsafe timing verification

## Future Enhancements

### v2.0 Roadmap
- Closed-loop speed control (encoders)
- Battery voltage monitoring
- Current limiting
- OTA firmware updates
- Telemetry logging (SD card)
- ROS/ROS2 integration

---

*Last updated: 2025-12-28*
