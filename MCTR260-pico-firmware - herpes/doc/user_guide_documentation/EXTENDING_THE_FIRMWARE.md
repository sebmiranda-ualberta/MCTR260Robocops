# Extending the Firmware

This guide shows you how to **add your own code** to the Pico W firmware. Whether you need an extra motor, a sensor, a gripper, or a custom control algorithm, this document walks you through it using the actual APIs and file structure of this codebase.

> **Hardware Platform:** All examples assume the MECHA PICO board. Pin assignments, MCP23017 addresses, and I2C bus topology are specific to this board.

---

## Table of Contents

1. [Before You Start](#before-you-start)
2. [Using Motor 5 (Spare Stepper)](#using-motor-5-spare-stepper)
3. [Using DC Motors 3 and 4](#using-dc-motors-3-and-4)
4. [Creating a Custom Motion Profile](#creating-a-custom-motion-profile)
5. [Using Aux Channels and Toggles](#using-aux-channels-and-toggles)
6. [Adding I2C Devices](#adding-i2c-devices)
7. [Common Pitfalls](#common-pitfalls)

---

## Before You Start

### What You Can Safely Modify

| Path | Safe to Edit? | Notes |
|------|:---:|-------|
| `src/project_config.h` | ✅ | This is the primary configuration file. Change device name, PIN, motor driver, geometry. |
| `src/profiles/` | ✅ | Add new profile files here. This is where your mission-specific code goes. |
| `src/main.cpp` | ⚠️ | You may add `#include` and `#ifdef` blocks for your new profiles. Do not modify the Core 1 loop or BLE callbacks. |
| `src/core/` | ❌ | BLE, command parsing, motor base classes, safety watchdog. Modifying these risks breaking real-time timing, Bluetooth, or safety. |
| `src/drivers/` | ❌ | MCP23017 driver and kinematics math. Only modify if you fully understand I2C bus sharing with Core 1. |

### The Extension Pattern

All extensions follow the same pattern:

1. **Create a new file** in `src/profiles/`
2. **Guard it** with a `#define` in `project_config.h`
3. **Read from** the `control_command_t` struct (joystick, aux, toggles)
4. **Write to** motors or I/O via the existing APIs
5. **Register it** in `main.cpp` with a `#ifdef` block

This pattern keeps your code isolated from the core firmware. If something goes wrong, you can disable your profile with a single comment and the base system still works.

---

## Using Motor 5 (Spare Stepper)

Motor 5 (M5) is a spare stepper motor pre-wired on MCP23017 U6_1 Port A. It is **already implemented** in the firmware and runs on **Core 1** with the same real-time pulse generation and acceleration ramping as the drive motors M1-M4. You do not need to write any code.

### How to Enable

In `src/project_config.h`, uncomment these lines:

```cpp
#define ENABLE_MOTOR_5                // Enable Motor 5
#define MOTOR_5_AUX_CHANNEL      0    // Which aux slider controls M5 (0-5)
#define MOTOR_5_MAX_SPEED     2000.0f // Max speed in steps/sec
#define MOTOR_5_DIR_INVERT       1    // 1 = normal, -1 = reverse
```

That's it. Compile and upload. The aux slider you selected in the Flutter app now controls Motor 5.

### How It Works

Motor 5 follows the same data path as M1-M4, but uses a separate Port A write:

```
Flutter App aux slider → BLE JSON → profile_aux_motors.cpp
  → g_targetSpeeds[4] (via mutex) → Core 1 simple_stepper
  → MCP23017 U6_1 Port A (GPA1=STEP, GPA2=DIR) → motor spins
```

Motors 1-4 are batch-written on Port B (2 I2C writes for all 4 motors). Motor 5 is written separately on Port A (2 additional I2C writes). Total I2C budget goes from 10% to 20%, well within the 500µs cycle.

| Setting | What It Does | Default |
|---------|-------------|---------|
| `MOTOR_5_AUX_CHANNEL` | Which aux slider (0-5) from the Flutter app | 0 |
| `MOTOR_5_MAX_SPEED` | Slider full-scale maps to this value (steps/sec) | 2000.0 |
| `MOTOR_5_DIR_INVERT` | Set to `-1` if the motor spins the wrong way | 1 |

---

## Using DC Motors 3 and 4

DC Motors 3 and 4 are pre-wired on MCP23017 U6_2 (address 0x21) Port A via H-bridge direction pins. These are **on/off direction control only** (no speed control over I2C). The firmware handles everything; you only configure `project_config.h`.

### How to Enable

In `src/project_config.h`, uncomment the motors you need:

```cpp
#define ENABLE_DC_MOTOR_3              // Enable DC Motor 3
#define DC_MOTOR_3_AUX_CHANNEL    1    // Which aux slider (0-5)
#define DC_MOTOR_3_DIR_INVERT     1    // 1 = normal, -1 = reverse

#define ENABLE_DC_MOTOR_4              // Enable DC Motor 4
#define DC_MOTOR_4_AUX_CHANNEL    2    // Which aux slider (0-5)
#define DC_MOTOR_4_DIR_INVERT     1    // 1 = normal, -1 = reverse
```

### Control Behavior

| Aux Slider Position | Motor Action |
|:---:|:---:|
| Above +5% | Forward (full speed) |
| Below -5% | Reverse (full speed) |
| Within ±5% (deadzone) | Off (motor stops) |

Because the MCP23017 is a digital GPIO expander (not PWM), there is no variable speed control. The motor is either running at full voltage or stopped.

### Hardware Mapping

| Signal | MCP23017 Pin | Bit |
|--------|:---:|:---:|
| DC Motor 3 negative | GPA2 | `IN_MOT_3N_BIT` |
| DC Motor 3 positive | GPA3 | `IN_MOT_3P_BIT` |
| DC Motor 4 negative | GPA0 | `IN_MOT_4N_BIT` |
| DC Motor 4 positive | GPA1 | `IN_MOT_4P_BIT` |

> [!NOTE]
> DC Motors 3-4 are on MCP23017 U6_2 (0x21), which is **not** accessed by Core 1. No mutex is needed for these writes.

---

## Creating a Custom Motion Profile

A motion profile is a function that receives every control command and drives one or more motors. The mecanum profile (`profile_mecanum.cpp`) and aux motors profile (`profile_aux_motors.cpp`) are the built-in examples.

### Anatomy of a Profile

Every profile has three parts:

1. **A `#define` guard** in `project_config.h` to enable/disable it
2. **An `apply` function** that receives `const control_command_t *cmd`
3. **A registration call** in `main.cpp`'s `onBleCommand()` function

### Minimal Profile Template

```cpp
// src/profiles/profile_yourname.cpp

#include "project_config.h"
#ifdef MOTION_PROFILE_YOURNAME

#include "core/command_packet.h"

void profile_yourname_apply(const control_command_t *cmd) {
    // Read inputs from the command struct:
    //   cmd->left.x, cmd->left.y     (left joystick)
    //   cmd->left.value              (left dial or slider)
    //   cmd->right.x, cmd->right.y   (right joystick)
    //   cmd->right.value             (right dial or slider)
    //   cmd->speed                   (speed slider, 0-100)
    //   cmd->aux[0..5]               (auxiliary sliders, -100 to +100)
    //   cmd->toggles[0..5]           (toggle switches, true/false)
    //   cmd->vehicle                 (vehicle type string)

    // Your logic here...
}

#endif
```

### What Inputs Are Available?

The `control_command_t` struct carries everything the Flutter app sends:

| Field | Type | Range | Sent When |
|-------|------|-------|-----------|
| `left.x`, `left.y` | float | -100 to +100 | Left control is a joystick |
| `left.value` | float | -100 to +100 (slider) or -135 to +135 (dial) | Left control is a dial or slider |
| `left.isJoystick` | bool | true/false | Always (tells you which fields to read) |
| `right.x`, `right.y` | float | -100 to +100 | Right control is a joystick |
| `right.value` | float | -100 to +100 (slider) or -135 to +135 (dial) | Right control is a dial or slider |
| `speed` | uint8_t | 0 to 100 | Always |
| `aux[0..5]` | float | -100 to +100 | Always (6 spare sliders) |
| `toggles[0..5]` | bool | true/false | Always (6 toggle switches) |
| `vehicle` | char[16] | String | Always (`"mecanum"`, `"fourwheel"`, etc.) |

---

## Using Aux Channels and Toggles

The Flutter app provides **6 auxiliary sliders** (`aux[0]` through `aux[5]`) and **6 toggle switches** (`toggles[0]` through `toggles[5]`). These are spare control channels for mission-specific features.

### Data Flow

```
Flutter App                    Pico W Firmware
┌──────────┐                  ┌──────────────────────┐
│ Aux      │    BLE JSON      │ command_parser.cpp   │
│ Slider 0 │ ──────────────►  │ cmd.aux[0] = 75.0    │
│  (75%)   │                  │                      │
│ Toggle 2 │ ──────────────►  │ cmd.toggles[2] = true│
│  (ON)    │                  └──────────┬───────────┘
└──────────┘                             │
                                         ▼
                              ┌────────────────────── ┐
                              │ Your profile reads:   │
                              │ cmd->aux[0]     → 75  │
                              │ cmd->toggles[2] → true│
                              └────────────────────── ┘
```

### Example: LED Control with a Toggle

```cpp
void profile_leds_apply(const control_command_t *cmd) {
    // Toggle 0 controls the LED bar
    if (cmd->toggles[0]) {
        // Turn on LED bar segments via MCP23017 U6_2
        mcpDC.setBitA(4, true);  // LED_BAR_2
        mcpDC.setBitA(5, true);  // LED_BAR_3
    } else {
        mcpDC.setBitA(4, false);
        mcpDC.setBitA(5, false);
    }
}
```

### Example: Gripper with Motor 5

If Motor 5 is wired to a gripper mechanism, enable it in `project_config.h`:

```cpp
#define ENABLE_MOTOR_5
#define MOTOR_5_AUX_CHANNEL      0    // Aux slider 0 controls the gripper
#define MOTOR_5_MAX_SPEED     500.0f  // Slower speed for gripper precision
#define MOTOR_5_DIR_INVERT       1
```

The aux slider in the Flutter app directly controls the gripper: push up to open, pull down to close, release to stop. No code required.

---

## Adding I2C Devices

The MECHA PICO board uses I2C0 (GP4=SDA, GP5=SCL) at 400 kHz. Two MCP23017 chips already live on this bus at addresses 0x20 and 0x21. You can add more I2C devices (sensors, displays, additional GPIO expanders) to the same bus.

### The Critical Rule: Core 1 Owns the Bus During Pulse Generation

Core 1 writes to MCP23017 address 0x20 (stepper control) every 500 microseconds. Any I2C access from Core 0 that collides with a Core 1 write will corrupt both transactions.

**Safe I2C access from Core 0:**

1. **Use the mutex.** Wrap your I2C reads/writes with the speed mutex:

```cpp
extern mutex_t g_speedMutex;

void read_my_sensor() {
    if (mutex_try_enter(&g_speedMutex, nullptr)) {
        // Safe to use I2C here - Core 1 is not accessing the bus
        uint8_t data;
        i2c_read_blocking(i2c0, MY_SENSOR_ADDR, &data, 1, false);

        mutex_exit(&g_speedMutex);
    }
    // If mutex was busy, skip this read and try next cycle
}
```

2. **Keep it short.** Hold the mutex for the minimum time possible. Core 1 will skip its pulse generation if it cannot acquire the lock.

3. **Use `mutex_try_enter`, not `mutex_enter_blocking`.** Blocking on Core 0 will stall the BLE stack.

### MCP23017 U6_2 Port B: Spare 5V I/O

The second MCP23017 (U6_2, address 0x21) has 8 spare general-purpose I/O pins on Port B:

| MCP Pin | Bit | Signal |
|:-------:|:---:|--------|
| GPB0 | 0 | `IO_5V_0` |
| GPB1 | 1 | `IO_5V_1` |
| GPB2 | 2 | `IO_5V_2` |
| GPB3 | 3 | `IO_5V_3` |
| GPB4 | 4 | `IO_5V_4` |
| GPB5 | 5 | `IO_5V_5` |
| GPB6 | 6 | `IO_5V_6` |
| GPB7 | 7 | `IO_5V_7` |

These operate at 5V logic level and can be used as outputs (LEDs, relays) or inputs (sensors, switches). The MCP23017 U6_2 is on address 0x21, which is **not** used by Core 1's stepper engine (Core 1 only writes to address 0x20). This means you can access U6_2 from Core 0 without mutex protection for the bus, but you still need to avoid I2C bus contention during Core 1's writes to 0x20.

---

## Common Pitfalls

### 1. Writing to MCP23017 0x20 from Core 0

Core 1's `simple_stepper_update()` writes to MCP23017 address 0x20 every 500 microseconds. If your Core 0 code also writes to address 0x20 (the stepper MCP) without the mutex, the I2C transactions will collide and produce garbage on the bus.

**Fix:** Only access the stepper MCP23017 (0x20) from Core 0 inside a `mutex_try_enter` block.

### 2. Using `delay()` in a Profile

`delay()` on Core 0 blocks the BLE event loop. A `delay(100)` will stall Bluetooth processing for 100ms, causing jitter in command reception and potentially triggering the safety watchdog.

**Fix:** Use timing variables instead:

```cpp
static unsigned long lastStepTime = 0;

void profile_apply(const control_command_t *cmd) {
    unsigned long now = micros();
    if (now - lastStepTime >= stepIntervalUs) {
        // Generate step pulse
        lastStepTime = now;
    }
}
```

### 3. Adding `Serial.printf()` in Core 1

Serial output on the Pico W takes approximately 8ms per print call. Adding any `Serial.printf()` inside `loop1()` or `simple_stepper_update()` will destroy step timing precision and cause visible motor jitter.

**Fix:** Never print from Core 1. Use a volatile flag to signal Core 0 to print.

### 4. Forgetting the `#ifdef` Guard

If your profile code is not wrapped in a `#ifdef MOTION_PROFILE_YOURNAME` / `#endif` guard, it will always compile and run, even when you think you have disabled it. This wastes CPU time and can cause unexpected motor behavior.

### 5. Exceeding the I2C Bus Bandwidth

Each I2C write at 400 kHz takes approximately 50 microseconds. Core 1 uses ~100 microseconds of every 500-microsecond cycle for its 2 batch writes. Adding many I2C transactions from Core 0 can starve Core 1 of bus time.

**Fix:** Keep your I2C access infrequent (once every 20ms or less) and short (1-2 bytes per transaction).
