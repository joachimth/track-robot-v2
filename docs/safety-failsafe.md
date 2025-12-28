# Safety and Failsafe System

Safety mechanisms and failsafe behavior.

## Safety States

### DISARMED (Default)
- Motors disabled
- Boot default state
- Safe for handling

### ARMED
- Motors enabled
- Accepts control inputs
- Requires explicit arming

### ESTOP (Emergency Stop)
- Latched state
- Motors immediately stopped
- Requires re-arm to clear

## State Transitions

```
DISARMED ─────ARM────→ ARMED
   ↑                     │
   │                     │
   └────DISARM───────────┘
   
   Any state ──ESTOP──→ ESTOP ──ARM──→ DISARMED
```

## Failsafe Mechanisms

### 1. Boot Disarmed
- System always boots in DISARMED state
- Prevents unexpected movement on power-up
- Must explicitly arm via START button or API

### 2. Watchdog Timeout
- Monitors control input freshness
- Timeout: 500ms (configurable)
- No input for 500ms → Auto-disarm
- Prevents runaway if controller disconnects

### 3. Emergency Stop
- Latched (doesn't auto-clear)
- Immediate motor stop (bypasses ramping)
- Requires explicit re-arm
- Activated by X button or `/estop` API

### 4. Control Arbitration
- Only one source active at a time
- Timeout reverts to safe stop
- No priority conflicts

## Testing Procedures

### Test 1: Boot Disarmed
1. Power on ESP32
2. Try moving sticks → Motors should NOT move
3. Press START → System arms
4. Move sticks → Motors respond

### Test 2: Watchdog Timeout
1. Arm system
2. Move sticks (motors respond)
3. Release sticks to center
4. Wait 600ms
5. System should auto-disarm (motors stop)

### Test 3: Emergency Stop
1. Arm system
2. Move sticks (motors respond)
3. Press X button → Motors stop immediately
4. Try moving sticks → Motors do NOT respond
5. Press START → System re-arms

### Test 4: Controller Disconnect
1. Arm system with PS3
2. Turn off controller (hold PS button 10s)
3. Within 600ms, system should auto-disarm

## LED Status Patterns

| Pattern | Meaning |
|---------|---------|
| Fast blink (100ms) | Boot |
| Slow blink (1s) | Disarmed |
| Solid ON | Armed |
| Very fast blink (50ms) | Emergency stop |

## Safety Best Practices

### Before First Power-On
- ✓ Visual inspection of all wiring
- ✓ Continuity test (common ground)
- ✓ Voltage test (buck converter = 5V)
- ✓ Tracks off the ground

### During Testing
- ✓ Always test with tracks off ground first
- ✓ Start in slow mode
- ✓ Have physical e-stop accessible
- ✓ Monitor BTS7960 temperature

### In Operation
- ✓ Never rely solely on firmware e-stop
- ✓ Physical switch on battery line
- ✓ Keep firmware e-stop accessible
- ✓ Monitor battery voltage

## Known Limitations

1. Firmware failsafe is NOT hardware failsafe
2. ESP32 crash = motors MAY continue (use physical switch!)
3. WiFi latency can delay HTTP estop
4. Bluetooth disconnects may take up to 600ms to detect

## Recommendations

- Add physical emergency stop switch (mandatory for field use)
- Fuse battery line (50A recommended)
- Consider adding current sensors for overcurrent shutdown
- Monitor battery voltage (add in firmware v2.0)

*Last updated: 2025-12-28*
