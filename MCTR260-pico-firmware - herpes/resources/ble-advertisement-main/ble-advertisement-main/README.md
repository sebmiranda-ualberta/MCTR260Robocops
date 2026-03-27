# Pico BLE Advertisment

This project provides a Bluetooth Low Energy (BLE) advertiser firmware for the Raspberry Pi Pico W / Pimoroni Inventor, intended to act as the BLE endpoint for a mobile controller app.

## Features
- BLE advertising with a custom, user-defined device name
- Proven BLE connection logging (connect / disconnect events)
- Visual heartbeat LED (slow blink = idle, fast blink = connected)
- Name is configurable from CMake, not hardcoded in source

## Hardware
- Raspberry Pi Pico W or Pimoroni Inventor
- USB data cable (must support data, not charge-only)

## Runnning
### Changing the BLE Device Name

The BLE name is configured once in CMake. To set the name:

_In CMakeLists.txt:_

```set(DEVICE_NAME "MCTR-Inventor" CACHE STRING "BLE device name")```

Or

_Override it at configure time:_

```cmake -S . -B build -DPICO_BOARD=pico_w -DDEVICE_NAME="MCTR-Inventor-Test"```

### Build & Flash
A clean rebuild is required whenever the device name changes

```
rm -rf build
cmake -S . -B build -DPICO_BOARD=pico_w
cmake --build build -j
```

### Flash to the Pico W

- Hold BOOTSEL on the inventor
- Plug the Pico into USB
- Release BOOTSEL
- Copy the generated .uf2 file from build/ onto RPI-RP2
  ```
  cp inventor_ble.uf2 /media/$USER/RPI-RP2/
  sync
  ```

### Start Advertisement
- Open a serial monitor on the correct port (usually /dev/ttyACM0)
- Start monitoring

### Connect BLE device to Inventor
Using a BLE scanner app: 
Scan for devices → device should appear as MCTR-Inventor
Connect to device

## Common Notes
BLE scanner app may not see updated inventor name right away becasue the cache still recognises the old name
