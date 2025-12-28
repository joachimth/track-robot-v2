# PWM Frequency Tuning Guide

This guide explains how to tune the PWM frequency for optimal motor control.

## Background

### What is PWM?

Pulse Width Modulation (PWM) controls motor speed by rapidly switching power on and off. The duty cycle (percentage of time "on") determines the average voltage and thus the speed.

### Frequency vs Resolution Trade-off

ESP32 LEDC formula:
```
frequency = 80,000,000 Hz / (2^resolution)
```

Examples:
- 8-bit (256 steps): 80MHz / 256 = 312.5 kHz
- 10-bit (1024 steps): 80MHz / 1024 = 78.1 kHz
- 12-bit (4096 steps): 80MHz / 4096 = 19.53 kHz
- 14-bit (16384 steps): 80MHz / 16384 = 4.88 kHz

## Current Default: 20kHz @ 12-bit

### Why 20kHz?

1. **Silent Operation**: Above human hearing range (~20kHz)
   - No motor whine or audible noise
   - Better user experience

2. **Excellent Resolution**: 4096 steps
   - 0.024% resolution per step
   - Very smooth speed control
   - Imperceptible steps

3. **BTS7960 Compatible**: Within spec
   - BTS7960 max frequency: ~25kHz
   - 20kHz leaves safety margin

4. **Industry Standard**: Common for high-quality motor control

### Actual Frequency

Due to integer division:
- Configured: 20,000 Hz
- Actual: 19,531 Hz (80MHz / 4096)
- Error: -2.3% (acceptable)

## Tuning Process

### Step 1: Understand Your Goals

**Choose based on priority**:

| Goal | Recommended Setting |
|------|---------------------|
| Silent operation | 20kHz @ 12-bit ✓ (default) |
| Maximum smoothness | 10kHz @ 13-bit |
| Very high frequency | 25kHz @ 12-bit (near BTS7960 limit) |
| Audible feedback | 4kHz @ 14-bit |

### Step 2: Modify Configuration

Edit `firmware/main/Kconfig.projbuild`:

```kconfig
config ROBOT_MOTOR_PWM_FREQ_HZ
    int "PWM frequency (Hz)"
    default 20000  # Change this
    range 1000 40000

config ROBOT_MOTOR_PWM_RESOLUTION
    int "PWM resolution (bits)"
    default 12     # Change this
    range 8 14
```

Or via `idf.py menuconfig`:
- `Component config → Robot Config → Motor Control`

### Step 3: Test Incrementally

#### Bench Test (Tracks OFF Ground)

1. **Start conservative**: 10kHz @ 12-bit
2. **Flash and power up**
3. **Arm system** (START button)
4. **Test throttle**:
   - Slowly increase from 0% to 100%
   - Listen for noise
   - Feel for vibration
   - Check smoothness

5. **Increase frequency**:
   - Try 15kHz
   - Then 20kHz
   - Then 25kHz
   - Stop if you hear noise or see issues

#### Field Test (Tracks ON Ground, Under Load)

1. **Repeat bench tests** with load
2. **Check thermal performance**:
   - Monitor BTS7960 temperature
   - Should stay below 60°C
   - Allow 5 minutes of continuous operation

3. **Listen for motor whine**:
   - Especially at low speeds (10-30%)
   - Should be silent at 20kHz

## Common Issues & Solutions

### Issue: Motor Whine

**Symptom**: High-pitched noise from motors

**Cause**: PWM frequency in audible range

**Solutions**:
- Increase frequency to ≥ 20kHz
- May need to reduce resolution if already at 12-bit
- Try 20kHz @ 11-bit (2048 steps, ~39kHz actual)

### Issue: Jerky Movement

**Symptom**: Motors don't move smoothly, visible steps

**Cause**: Resolution too low

**Solutions**:
- Increase resolution (10-bit → 12-bit)
- May need to reduce frequency
- Try 10kHz @ 13-bit (8192 steps)

### Issue: BTS7960 Overheating

**Symptom**: Drivers get very hot (>80°C)

**Cause**: Frequency too high, excessive switching losses

**Solutions**:
- Reduce frequency to 15kHz or 10kHz
- Add heatsink to BTS7960
- Improve airflow
- Check for short circuits

### Issue: Motor Stuttering at Low Speeds

**Symptom**: Motors stutter or don't start smoothly

**Causes**:
1. Frequency too low (< 5kHz)
2. Deadband in BTS7960
3. Motor cogging

**Solutions**:
- Increase frequency to 10-20kHz
- Increase minimum PWM duty (e.g., 5% threshold)
- Check motor condition

### Issue: ESP32 Crashes

**Symptom**: ESP32 resets or WDT triggers

**Cause**: Invalid frequency/resolution combination

**Solutions**:
- Verify formula: 80MHz / (2^res) yields valid frequency
- Stay within ranges: 1kHz - 40kHz, 8-14 bit
- Check for memory issues (stack overflow)

## Advanced Tuning

### Per-Motor Calibration

If left and right motors respond differently:

1. **Measure actual motor characteristics**:
   - Stall current
   - No-load current
   - RPM vs voltage curve

2. **Adjust per-motor settings** (future feature):
   - Separate PWM frequencies
   - Per-motor duty offset
   - Calibration tables

### Frequency Sweep Test

To find optimal frequency empirically:

```python
# Test script (run with motors unloaded)
for freq in [5000, 10000, 15000, 20000, 25000]:
    for res in [10, 11, 12, 13]:
        if 80000000 / (2**res) >= freq:
            print(f"Testing {freq}Hz @ {res}-bit")
            # Flash firmware with these settings
            # Measure: noise level, smoothness, temperature
            # Record results
```

## Measurement Tools

### Oscilloscope

**Recommended**: Measure actual PWM output

1. Connect probe to LPWM or RPWM pin
2. Trigger on rising edge
3. Measure:
   - Frequency (should match configured ±2%)
   - Duty cycle at various throttle values
   - Rise/fall times (should be <1μs)

### Multimeter (Frequency Mode)

**Quick check**:
- Set to frequency measurement
- Connect to PWM pin
- Should read ~20kHz at any non-zero throttle

### Sound Level Meter

**Check for audible noise**:
- Run motors at 50% throttle
- Measure dB level
- Should not increase at 20kHz vs silence

## Configuration Examples

### Example 1: Maximum Silence

```
Frequency: 20000 Hz
Resolution: 12-bit
Actual: 19531 Hz
Steps: 4096
Use case: Indoor robot, residential use
```

### Example 2: Maximum Smoothness

```
Frequency: 10000 Hz
Resolution: 13-bit
Actual: 9766 Hz
Steps: 8192
Use case: Precision control, slow speeds
Note: May have audible hum below 15kHz
```

### Example 3: High Frequency

```
Frequency: 25000 Hz
Resolution: 11-bit
Actual: 39062 Hz (!)
Steps: 2048
Use case: BTS7960 limit testing
Warning: Near driver maximum, monitor temperature
```

### Example 4: Debugging

```
Frequency: 1000 Hz
Resolution: 14-bit
Actual: 4883 Hz
Steps: 16384
Use case: Slow, visible PWM for troubleshooting
Note: Extremely audible, testing only
```

## Validation Checklist

Before finalizing PWM settings:

- [ ] No audible motor whine
- [ ] Smooth motor response (no jerking)
- [ ] BTS7960 temperature < 60°C under load
- [ ] Full speed range achievable (0-100%)
- [ ] Low speed control adequate (10-20%)
- [ ] No ESP32 crashes or WDT triggers
- [ ] Oscilloscope confirms frequency (if available)
- [ ] Battery current draw normal (no excessive spikes)

## Troubleshooting Decision Tree

```
Is there motor whine?
 ├─ YES → Increase frequency (or reduce load)
 └─ NO → Is movement smooth?
          ├─ YES → Is BTS7960 cool (<60°C)?
          │        ├─ YES → Configuration OK ✓
          │        └─ NO → Reduce frequency
          └─ NO → Increase resolution
```

## References

- [ESP32 LEDC Documentation](https://docs.espressif.com/projects/esp-idf/en/v5.1.2/esp32/api-reference/peripherals/ledc.html)
- [BTS7960 Datasheet](https://www.infineon.com/dgdl/Infineon-BTS7960-DS-v01_00-EN.pdf)
- [Motor Control PWM Best Practices](https://www.ti.com/lit/an/sprabx5/sprabx5.pdf)

---

*Last updated: 2025-12-28*
