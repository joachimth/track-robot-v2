Automated rolling build from the `main` branch.

This pre-release is overwritten on every merge to `main`.
For stable versioned firmware use a tagged release (e.g. `v1.0.0`).

### Flash via web flasher
https://joachimth.github.io/track-robot-v2/

### Flash manually
```
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
  --before default_reset --after hard_reset write_flash \
  0x1000 bootloader.bin 0x8000 partition-table.bin 0x10000 track-robot.bin
```
