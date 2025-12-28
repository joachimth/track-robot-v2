# PS3 Controller Component

This directory should contain the `esp32-ps3` component as a git submodule.

## Setup

After cloning this repository, initialize the submodule:

```bash
cd firmware/components/ps3
git submodule add https://github.com/joachimth/esp32-ps3.git .
```

Or when cloning the main repository:

```bash
git clone --recursive https://github.com/joachimth/track-robot-v2.git
```

## Manual Installation

If not using submodules, you can manually clone:

```bash
cd firmware/components
git clone https://github.com/joachimth/esp32-ps3.git ps3
```

The PS3 library provides Bluetooth Classic support for Sony PS3 controllers on ESP32.
