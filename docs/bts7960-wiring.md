# IBT-2 / BTS7960 Wiring Guide

Complete wiring guide for connecting IBT-2 (BTS7960) motor drivers to the ESP32 and motors.

## BTS7960 Overview

| Specification | Value |
|--------------|-------|
| Max current | 43 A continuous |
| Operating voltage | 5.5 V – 27 V |
| Logic voltage | 3.3 V / 5 V compatible |
| PWM frequency | Up to ~25 kHz |

**Pin functions**:

| Pin | Direction | Description |
|-----|-----------|-------------|
| RPWM | Input | Reverse (backward) PWM |
| LPWM | Input | Forward PWM |
| R_EN | Input | Reverse half-bridge enable (active high) |
| L_EN | Input | Forward half-bridge enable (active high) |
| R_IS | Output | Reverse current sense (ADC, optional) |
| L_IS | Output | Forward current sense (ADC, optional) |
| VCC | Power | Logic supply 5 V |
| GND | Power | Logic ground |
| B+ | Power | Battery positive (12 V) |
| B- | Power | Battery negative |
| M+ | Motor | Motor terminal positive |
| M- | Motor | Motor terminal negative |

---

## Wiring Diagrams

### Left Motor — IBT-2 #1 (Track A)

```
ESP32-WROOM-32         IBT-2 #1               Motor Left
┌──────────────┐      ┌──────────┐           ┌──────────┐
│              │      │          │           │          │
│  GPIO 27 ───────→  RPWM       │           │          │
│  GPIO 14 ───────→  LPWM       │           │          │
│  GPIO 25 ───────→  R_EN       │           │          │
│  GPIO 26 ───────→  L_EN       │           │          │
│              │      │          │           │          │
│  GPIO 34 ←───────  R_IS  (optional ADC)   │          │
│  GPIO 35 ←───────  L_IS  (optional ADC)   │          │
│              │      │          │           │          │
│  5V ─────────────→  VCC        │           │          │
│  GND ────────────→  GND        │           │          │
│              │      │          │           │          │
│              │      │  M+ ──────────────→  +         │
│              │      │  M- ──────────────→  -         │
│              │      │          │           │          │
│              │      │  B+ ←── 12 V Battery           │
│              │      │  B- ←── Battery GND            │
└──────────────┘      └──────────┘           └──────────┘
```

### Right Motor — IBT-2 #2 (Track B)

```
ESP32-WROOM-32         IBT-2 #2               Motor Right
┌──────────────┐      ┌──────────┐           ┌──────────┐
│              │      │          │           │          │
│  GPIO 18 ───────→  RPWM       │           │          │
│  GPIO 19 ───────→  LPWM       │           │          │
│  GPIO 33 ───────→  R_EN       │           │          │
│  GPIO 32 ───────→  L_EN       │           │          │
│              │      │          │           │          │
│  GPIO 36 ←───────  R_IS  (optional ADC)   │          │
│  GPIO 39 ←───────  L_IS  (optional ADC)   │          │
│              │      │          │           │          │
│  5V ─────────────→  VCC        │           │          │
│  GND ────────────→  GND        │           │          │
│              │      │          │           │          │
│              │      │  M+ ──────────────→  +         │
│              │      │  M- ──────────────→  -         │
│              │      │          │           │          │
│              │      │  B+ ←── 12 V Battery           │
│              │      │  B- ←── Battery GND            │
└──────────────┘      └──────────┘           └──────────┘
```

---

## Power Distribution

```
Milwaukee 12 V Battery
    │
    ├── IBT-2 #1 B+  (motor power left)
    ├── IBT-2 #2 B+  (motor power right)
    │
    └── Buck Converter input (12 V)
              │
              └── Buck Converter output (5 V)
                        ├── ESP32 VIN
                        ├── IBT-2 #1 VCC
                        └── IBT-2 #2 VCC
```

---

## ★ Critical: Common Ground

**ALL grounds must be tied together at a single point:**

| Connection | Why |
|-----------|-----|
| Battery negative | Reference for all voltages |
| ESP32 GND | Logic reference |
| IBT-2 #1 GND | Logic reference |
| IBT-2 #1 B- | Motor power return |
| IBT-2 #2 GND | Logic reference |
| IBT-2 #2 B- | Motor power return |
| Buck converter GND | Logic supply return |

> **Failing to connect a common ground causes**: erratic motor behaviour,
> ESP32 crashes, damaged I/O pins, and communication failures.

---

## Complete Pin Reference

| ESP32 GPIO | Signal | IBT-2 Left | IBT-2 Right | Notes |
|-----------|--------|------------|-------------|-------|
| 27 | RPWM | RPWM | — | Left backward PWM |
| 14 | LPWM | LPWM | — | Left forward PWM |
| 25 | R_EN | R_EN | — | Left reverse enable |
| 26 | L_EN | L_EN | — | Left forward enable |
| 18 | RPWM | — | RPWM | Right backward PWM |
| 19 | LPWM | — | LPWM | Right forward PWM |
| 33 | R_EN | — | R_EN | Right reverse enable |
| 32 | L_EN | — | L_EN | Right forward enable |
| 34 | ADC | R_IS | — | Left current sense (input only) |
| 35 | ADC | L_IS | — | Left current sense (input only) |
| 36 | ADC | — | R_IS | Right current sense (input only) |
| 39 | ADC | — | L_IS | Right current sense (input only) |
| 2 | GPIO | — | — | Status LED |
| 5V | VCC | VCC | VCC | Logic power |
| GND | GND | GND + B- | GND + B- | Common ground |

> **Note**: GPIO 34, 35, 36, 39 are input-only pins on the ESP32-WROOM-32.
> Current sense is wired but not yet used in firmware v0.1.0.

---

## Safety Recommendations

1. Install a physical e-stop switch on the battery positive line
2. Fuse the battery line (50 A main fuse, 30 A per motor branch)
3. Use 12 AWG or thicker wire for motor power
4. Attach heatsinks to IBT-2 modules for sustained operation
5. Ensure airflow around drivers

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| Motors don't move | Enable pins not high | Check R_EN / L_EN wiring |
| Motors move wrong direction | RPWM / LPWM swapped | Swap wires, or use `invert_*` Kconfig option |
| Only one direction works | One PWM pin disconnected | Check RPWM and LPWM continuity |
| Erratic motion | Missing common ground | Connect all GNDs to single point |
| IBT-2 overheating | Excessive duty cycle or stall | Add heatsink; check for motor stall |
| ESP32 resets under load | Insufficient logic power | Verify buck converter output is stable 5 V |

*Last updated: 2026-05-08*
