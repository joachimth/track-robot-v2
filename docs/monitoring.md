# Motor Current & Battery Monitoring

## Purpose

The `monitor` component (`firmware/components/monitor/`) adds two Phase-2 safety
features:

1. **Over-current protection** — samples the IBT-2 `R_IS`/`L_IS` current-sense
   pins for both motors at ~10 Hz and latches an emergency stop if either motor
   exceeds a configurable threshold.
2. **Battery voltage monitoring** — optionally samples a resistor divider on the
   battery at ~1 Hz and logs a warning below a configurable threshold.

Both are surfaced in `GET /status` (the `monitor` block) and the **Control** tab
of the web UI.

## Context

The BTS7960 half-bridges on each IBT-2 board expose a current-sense output
(`R_IS` for the forward half, `L_IS` for the reverse half). These are already
wired to the ESP32's input-only ADC pins:

| ESP32 GPIO | Signal | ADC |
|-----------|--------|-----|
| 34 | Left  `R_IS` | ADC1_CH6 |
| 35 | Left  `L_IS` | ADC1_CH7 |
| 36 | Right `R_IS` | ADC1_CH0 (VP) |
| 39 | Right `L_IS` | ADC1_CH3 (VN) |

> **ADC1 only.** Wi-Fi is always active in this firmware (HTTP controller AP),
> and ADC2 cannot be used while Wi-Fi is on. All four current-sense pins are on
> ADC1, and any battery pin **must** also be on ADC1. The driver verifies this
> at init and skips (with a warning) any pin that resolves to ADC2.

For each motor the reported current is the larger of its two IS readings (only
one half conducts at a time depending on direction).

## Details

### ADC sampling

Uses the ESP-IDF oneshot ADC driver (`esp_adc`) on `ADC_UNIT_1` with
`ADC_ATTEN_DB_11` (≈0–3.1 V usable range). GPIO→channel mapping is resolved at
runtime via `adc_oneshot_io_to_channel()` — no hard-coded channel table.
Hardware calibration (line fitting) is enabled when available; otherwise the
driver falls back to an approximate linear scaling and logs a warning.

### Current scaling (calibration required)

Raw ADC millivolts are converted to load current with one calibration constant:

```
current_A = adc_mV / CONFIG_ROBOT_CURRENT_MV_PER_A
```

For a BTS7960 (current-sense ratio `k_ILIS` ≈ 8500) feeding a 1 kΩ resistor to
ground on the IS pin, the sense voltage is ≈ 118 mV per amp of load — hence the
default `CONFIG_ROBOT_CURRENT_MV_PER_A = 118`. **This must be calibrated on real
hardware**: drive a known load (or use a clamp meter), read `left_ma`/`right_ma`
from `GET /status`, and adjust the constant until they match.

### Battery scaling

Battery voltage is reconstructed from the divider:

```
battery_mV = adc_mV * (R1 + R2) / R2
```

with `R1` = top resistor (battery+ → ADC node) and `R2` = bottom resistor (ADC
node → GND). Defaults are `R1 = 10 kΩ`, `R2 = 3.3 kΩ`.

### Over-current response

When either motor's current exceeds `CONFIG_ROBOT_OVERCURRENT_MA` the monitor
calls `motor_emergency_stop()` (immediate) and `safety_emergency_stop()`
(latched E-STOP). The latch clears only on `POST /estop-reset` or reboot, like
any other E-STOP (see [safety-failsafe.md](safety-failsafe.md)). The trigger is
edge-detected so a sustained overload logs once, not every 100 ms.

## Hardware: battery voltage divider

Battery monitoring is **disabled by default** (`CONFIG_ROBOT_BATTERY_ADC_PIN =
-1`) because all four input-only ADC1 pins are already used by current sense. To
enable it:

1. Pick a **free ADC1-capable GPIO**. Options on ESP32:
   - GPIO 32 / 33 — only if you are not using them for the right-motor enable
     pins (they default to `R_EN`/`L_EN`).
   - GPIO 37 / 38 — ADC1 capable but not broken out on most DevKitC boards.
2. Build a divider from battery+ to that pin to GND, e.g. `R1 = 10 kΩ`,
   `R2 = 3.3 kΩ`. Size it so the ADC node stays **below ~3.1 V** at your maximum
   battery voltage (DB_11 full scale). For a 12 V pack peaking near 12.6 V, 10 k
   / 3.3 k gives ≈3.12 V — close to the ceiling; use 10 k / 3.0 k for headroom.
3. Set `CONFIG_ROBOT_BATTERY_ADC_PIN` to that GPIO and `CONFIG_ROBOT_BATTERY_R1_OHMS`
   / `CONFIG_ROBOT_BATTERY_R2_OHMS` to your actual resistor values.

```
Battery + ──[ R1 ]──┬── ADC pin
                     │
                   [ R2 ]
                     │
Battery - ───────────┴── GND
```

## Configuration (Kconfig → Robot Configuration → Robot Monitoring)

| Option | Default | Notes |
|--------|---------|-------|
| `CONFIG_ROBOT_ENABLE_MONITOR` | `y` | Master enable |
| `CONFIG_ROBOT_OVERCURRENT_MA` | `30000` | Per-motor E-STOP threshold (30 A) |
| `CONFIG_ROBOT_CURRENT_MV_PER_A` | `118` | IS-pin scaling — **calibrate** |
| `CONFIG_ROBOT_BATTERY_ADC_PIN` | `-1` | `-1` disables battery monitoring |
| `CONFIG_ROBOT_BATTERY_LOW_MV` | `10000` | Low-battery warning (10 V) |
| `CONFIG_ROBOT_BATTERY_R1_OHMS` | `10000` | Divider top resistor |
| `CONFIG_ROBOT_BATTERY_R2_OHMS` | `3300` | Divider bottom resistor |

## Troubleshooting

- **Current always reads ~0** — confirm the IBT-2 IS pins are wired to GPIO
  34/35/36/39 and that a sense resistor to GND is present; verify
  `CONFIG_ROBOT_CURRENT_MV_PER_A` against a known load.
- **Spurious over-current E-STOP** — PWM switching noise can spike IS readings;
  raise `CONFIG_ROBOT_OVERCURRENT_MA` or add an RC filter on the IS pin.
- **`battery_enabled` is false** — `CONFIG_ROBOT_BATTERY_ADC_PIN` is still `-1`
  or resolved to ADC2; pick a free ADC1 pin.
- **Monitor inactive at boot** — the serial log prints `Monitoring inactive` if
  no ADC inputs could be configured; the robot still runs normally.

*Last updated: 2026-06-01*
