# PS4 DualShock 4 Controller Setup

Guide for pairing and using a PS4 DualShock 4 controller with the tracked robot.

## How It Works

The firmware uses ESP-IDF's built-in Bluetooth Classic HID host (`esp_hidh`).
No third-party library is needed. On boot the ESP32 runs a ~10-second BT
inquiry scan — put the controller into pairing mode during this window.

## Pairing

### First-Time Pairing

Put the controller into **Bluetooth pairing mode** by holding
**PS + Share** simultaneously for ~3 seconds until the light bar flashes rapidly.

![PS4 pairing: hold PS and Share together](images/ps4-pairing.png)

The ESP32 detects any device advertising the name `Wireless Controller`
and connects automatically. You will see on the serial monitor:

```
I (xxxx) ps4: BT discovery started — press PS+Share on PS4
I (xxxx) ps4: PS4 found: Wireless Controller — connecting
I (xxxx) ps4: PS4 controller connected
```

### Re-Connecting After Reboot

Once paired, the PS4 controller remembers the ESP32's Bluetooth address.
On subsequent boots:

1. Power on the ESP32 (it starts a new scan automatically)
2. Press the **PS button** once on the controller
3. Controller reconnects within a few seconds

> **Tip**: The scan window is ~10 seconds. If pairing fails, simply reboot
> the ESP32 and try again.

## Button Mapping

| PS4 Input | Robot Action |
|-----------|-------------|
| Left stick Y-axis | Throttle: push up = forward, down = reverse |
| Left stick X-axis | Steering: push right = turn right |
| **Options** (≡) | **Arm** system (enable motors) |
| **Cross** (✕) | **Emergency stop** (latched — requires re-arm) |
| **L1** | **Slow mode** toggle (50% speed while held) |
| PS button | — (used for pairing/connection only) |
| Other buttons | Currently unused |

> **Controls summary**: Left stick drives, Options to arm, Cross to stop.

## Arming Procedure

Motors are **always disarmed at boot** for safety.

1. Power on ESP32 and wait for "System Ready" on serial monitor
2. Press **PS button** to connect controller (or pair with PS+Share)
3. Wait for "PS4 controller connected" message
4. Press **Options** to arm — you'll hear a relay click (if fitted)
5. Drive with left stick

## Safety

- **Cross button** triggers an immediate latched emergency stop
- After an e-stop, press **Options** again to re-arm
- If the controller disconnects, the failsafe timeout (500 ms) stops motors automatically

## Troubleshooting

### Controller Not Found During Scan

| Symptom | Fix |
|---------|-----|
| Scan expires with no device found | Re-enter pairing mode (PS+Share) and reboot ESP32 |
| Controller previously paired to PS4 console | Hold PS+Share on the controller to force pairing mode |
| Multiple BT devices nearby | Move away from other active BT devices during pairing |

### Controller Disconnects Frequently

- **Low battery** → charge controller via USB-C
- **Range** → stay within ~8 m of ESP32
- **Interference** → move away from 2.4 GHz Wi-Fi routers

### Wrong Direction of Travel

If forward/reverse are swapped:
```
idf.py menuconfig
→ Robot Configuration → Motor Control → Invert left/right motor
```

If left/right turns are reversed, swap the sign on steering in
`firmware/components/control/controller_ps4.c`:
```c
frame.steering = control_clamp(-g->lx);  // add minus sign
```

### Analog Stick Drift

Increase the deadzone:
```
idf.py menuconfig
→ Robot Configuration → Differential Drive → Deadzone
# Increase from 5% to 10%
```

## Customising Button Mapping

Edit `firmware/components/control/controller_ps4.c`:

```c
static void ps4_input_cb(const ps4_gamepad_t *g) {
    control_frame_t frame = {0};
    frame.timestamp  = xTaskGetTickCount();
    frame.throttle   = control_clamp(-g->ly);   // left stick Y, inverted
    frame.steering   = control_clamp(g->lx);    // left stick X
    frame.slow_mode  = g->l1;                   // L1 = slow mode
    frame.arm        = g->options;              // Options = arm
    frame.estop      = g->cross;               // Cross = e-stop
    control_manager_submit(CONTROL_SOURCE_PS4, &frame);
}
```

Available fields on `ps4_gamepad_t`: `lx`, `ly`, `rx`, `ry`, `cross`,
`circle`, `square`, `triangle`, `l1`, `r1`, `l2`, `r2`, `options`, `ps`.

*Last updated: 2026-05-08*
