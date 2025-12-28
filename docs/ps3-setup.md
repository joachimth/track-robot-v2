# PS3 Controller Setup Guide

Guide for pairing and using PS3 controllers with the tracked robot.

## Prerequisites

- Sony PS3 DualShock 3 controller (genuine or compatible)
- USB cable (Mini-USB)
- Computer (Linux, Mac, or Windows)
- Tool to read/write controller MAC address

## Step 1: Get Controller MAC Address

### Linux/Mac

Use `sixpair` tool:

```bash
# Clone sixpair
git clone https://github.com/user-none/sixpair.git
cd sixpair
make

# Connect controller via USB
# Run sixpair
sudo ./sixpair

# Output will show:
# Current Bluetooth master: 01:02:03:04:05:06
# Setting master to: aa:bb:cc:dd:ee:ff
```

Note the "Current Bluetooth master" address.

### Windows

Use SCP Toolkit or DS3 Tool to read the controller's Bluetooth address.

## Step 2: Set MAC Address in Firmware

Edit `firmware/main/main.c`:

```c
// Replace with your controller's MAC address
uint8_t ps3_mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
```

Example: If controller MAC is `AA:BB:CC:DD:EE:FF`, use:
```c
uint8_t ps3_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
```

## Step 3: Build and Flash Firmware

```bash
cd firmware
idf.py build flash monitor
```

Watch serial output for:
```
I (xxx) ctrl_ps3: PS3 controller initialized
I (xxx) ctrl_ps3:   Waiting for connection... (press PS button)
```

## Step 4: Pair Controller

1. **Disconnect USB** from controller
2. **Press PS button** (center button)
3. LEDs will blink rapidly
4. After 5-10 seconds, LEDs stabilize (usually LED 1 solid)
5. Serial monitor shows: `I (xxx) ctrl_ps3: PS3 controller connected!`

## Controller Mapping

### Analog Sticks

- **Left Stick Y-axis**: Throttle
  - Up = Forward (+1.0)
  - Down = Reverse (-1.0)
  - Center = Stop (0.0)

- **Right Stick X-axis**: Steering
  - Right = Turn right (+1.0)
  - Left = Turn left (-1.0)
  - Center = Straight (0.0)

### Buttons

- **START**: Arm system (enable motors)
- **X**: Emergency stop (latched, requires re-arm)
- **Triangle**: Toggle slow mode (50% speed)
- Other buttons: Currently unused (available for future features)

## Troubleshooting

### Controller Won't Connect

**Check**:
1. MAC address in firmware matches controller
2. ESP32 Bluetooth enabled (check sdkconfig)
3. Controller charged (connect USB to charge)
4. No other device paired to controller

**Fix**:
```bash
# Re-run sixpair to reset controller
sudo ./sixpair

# Flash firmware again
idf.py flash
```

### Controller Disconnects Frequently

**Causes**:
- Low battery → Charge controller
- Interference → Move away from WiFi routers
- Range → Stay within 10m of ESP32

### Analog Sticks Drift

**Causes**:
- Stick centering issue
- Increase deadzone in config

**Fix**:
```bash
idf.py menuconfig
# Robot Configuration → Differential Drive → Deadzone
# Increase from 5% to 10%
```

### Wrong Direction

**If forward/reverse inverted**:
```bash
idf.py menuconfig
# Robot Configuration → Motor Control → Invert left/right motor
```

**If left/right inverted**:
- Swap steering logic in `controller_ps3.c` (change sign)

## Advanced

### Multiple Controllers

Current firmware supports one controller at a time. To switch:
1. Turn off current controller (hold PS button 10s)
2. Update MAC address in firmware
3. Reflash
4. Connect new controller

### Battery Monitoring

Check battery level:
- Serial monitor shows battery % on connection
- Low battery warning at <20%

### Custom Button Mapping

Edit `controller_ps3.c` to change mapping:
```c
// Example: Use Circle for e-stop instead of X
frame.estop = ps3.button.circle;  // Change from ps3.button.cross
```

*Last updated: 2025-12-28*
