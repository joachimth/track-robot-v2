# BTS7960 Wiring Guide

Complete wiring guide for connecting BTS7960 motor drivers to ESP32 and motors.

## BTS7960 Overview

**Specifications**:
- Max current: 43A continuous
- Operating voltage: 5.5V - 27V
- Logic voltage: 3.3V - 5V compatible
- PWM frequency: Up to ~25kHz

**Pins**:
- **RPWM**: Reverse PWM input (3.3V logic)
- **LPWM**: Forward PWM input (3.3V logic)
- **R_EN**: Reverse enable (3.3V logic, active high)
- **L_EN**: Forward enable (3.3V logic, active high)
- **R_IS**: Reverse current sense (optional, not used)
- **L_IS**: Forward current sense (optional, not used)
- **VCC**: Logic power (5V)
- **GND**: Logic ground
- **M+**: Motor positive
- **M-**: Motor negative
- **B+**: Battery positive (12V)
- **B-**: Battery negative

## Wiring Diagram

### Left Motor (BTS7960 #1)

```
ESP32 DevKitC          BTS7960 #1            Motor Left
┌─────────────┐       ┌───────────┐         ┌──────────┐
│             │       │           │         │          │
│ GPIO 25 ────┼──────→│ RPWM      │         │          │
│ GPIO 26 ────┼──────→│ LPWM      │         │          │
│ GPIO 32 ────┼──────→│ R_EN      │         │          │
│ GPIO 33 ────┼──────→│ L_EN      │         │          │
│             │       │           │         │          │
│ 5V ─────────┼──────→│ VCC       │         │          │
│ GND ────────┼──────→│ GND       │         │          │
│             │       │           │         │          │
│             │       │ M+ ───────┼────────→│ +        │
│             │       │ M- ───────┼────────→│ -        │
│             │       │           │         │          │
│             │       │ B+ ←──────┼─────12V Battery    │
│             │       │ B- ←──────┼─────GND            │
└─────────────┘       └───────────┘         └──────────┘
```

### Right Motor (BTS7960 #2)

```
ESP32 DevKitC          BTS7960 #2            Motor Right
┌─────────────┐       ┌───────────┐         ┌──────────┐
│             │       │           │         │          │
│ GPIO 27 ────┼──────→│ RPWM      │         │          │
│ GPIO 14 ────┼──────→│ LPWM      │         │          │
│ GPIO 12 ────┼──────→│ R_EN      │         │          │
│ GPIO 13 ────┼──────→│ L_EN      │         │          │
│             │       │           │         │          │
│ 5V ─────────┼──────→│ VCC       │         │          │
│ GND ────────┼──────→│ GND       │         │          │
│             │       │           │         │          │
│             │       │ M+ ───────┼────────→│ +        │
│             │       │ M- ───────┼────────→│ -        │
│             │       │           │         │          │
│             │       │ B+ ←──────┼─────12V Battery    │
│             │       │ B- ←──────┼─────GND            │
└─────────────┘       └───────────┘         └──────────┘
```

## Power Distribution

```
Milwaukee 12V Battery
    │
    ├─── BTS7960 #1 B+
    ├─── BTS7960 #2 B+
    │
    └─── Buck Converter Input (12V)
             │
             └─── Buck Converter Output (5V)
                      │
                      ├─── ESP32 VIN (or 5V pin)
                      ├─── BTS7960 #1 VCC
                      └─── BTS7960 #2 VCC
```

## Critical: Common Ground

**⚠️ ALL GROUNDS MUST BE CONNECTED**:
- Battery negative (GND)
- ESP32 GND
- BTS7960 #1 GND (logic)
- BTS7960 #1 B- (power)
- BTS7960 #2 GND (logic)
- BTS7960 #2 B- (power)
- Buck converter GND

**Failure to connect common ground will result in**:
- Erratic motor behavior
- Damaged components
- ESP32 crashes
- Communication failures

## Pin Mapping Table

| ESP32 GPIO | Function | BTS7960 Left | BTS7960 Right |
|------------|----------|--------------|---------------|
| 25 | RPWM | RPWM | — |
| 26 | LPWM | LPWM | — |
| 32 | R_EN | R_EN | — |
| 33 | L_EN | L_EN | — |
| 27 | RPWM | — | RPWM |
| 14 | LPWM | — | LPWM |
| 12 | R_EN | — | R_EN |
| 13 | L_EN | — | L_EN |
| 5V | Logic Power | VCC | VCC |
| GND | Ground | GND + B- | GND + B- |

## Safety Recommendations

1. **Physical E-Stop**: Install a physical switch on battery positive
2. **Fusing**: Add 50A fuse on battery positive line
3. **Current Limiting**: Consider 30A fuse per motor
4. **Wire Gauge**: Use 12AWG or thicker for motor power
5. **Heatsinking**: Attach heatsinks to BTS7960 if continuous use
6. **Ventilation**: Ensure airflow around drivers

## Troubleshooting

### Motors Don't Move
- Check enable pins (R_EN, L_EN) are HIGH
- Verify 12V power to B+ on BTS7960
- Check motor connections (M+, M-)
- Verify PWM signals with oscilloscope

### One Direction Works, Other Doesn't
- Check RPWM vs LPWM wiring
- Verify both enable pins connected
- Test by swapping RPWM/LPWM

### Erratic Behavior
- ✓ Common ground connected?
- ✓ Logic power (VCC) stable at 5V?
- ✓ Signal wires not near motor power wires?

### BTS7960 Overheating
- Reduce PWM frequency (try 15kHz)
- Add heatsink
- Reduce motor current (slow down)
- Check for motor stall

*Last updated: 2025-12-28*
