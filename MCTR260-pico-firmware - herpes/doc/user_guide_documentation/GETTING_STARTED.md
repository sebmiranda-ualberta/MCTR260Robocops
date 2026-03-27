# Getting Started

This guide walks you through uploading the firmware to a Pico W for the **first time**. Every step is explained. If you have used PlatformIO before, you can skip to [Configure Your Robot](#3-configure-your-robot).

---

## Table of Contents

1. [Install VS Code](#1-install-vs-code)
2. [Install PlatformIO](#2-install-platformio)
3. [Configure Your Robot](#3-configure-your-robot)
4. [Build the Firmware](#4-build-the-firmware)
5. [Upload to the Pico W](#5-upload-to-the-pico-w)
6. [Verify It Works](#6-verify-it-works)
7. [Pair with the Flutter App](#7-pair-with-the-flutter-app)
8. [Test Your Motors](#8-test-your-motors)
9. [What Just Happened?](#9-what-just-happened)

---

## 1. Install VS Code

VS Code is a free code editor made by Microsoft. It is what you will use to view, edit, and upload the firmware.

1. Go to [code.visualstudio.com](https://code.visualstudio.com/)
2. Download the installer for your operating system
3. Run the installer with default settings

> [!TIP]
> **Using a VS Code fork?** Editors like Cursor AI, Windsurf, and VS Codium are built on the same VS Code base and fully support PlatformIO. If you already use one of these, install the PlatformIO extension from the Extensions panel and everything in this guide applies identically.

---

## 2. Install PlatformIO

PlatformIO is a VS Code extension that knows how to compile C++ code for microcontrollers (like the Pico W) and upload it over USB.

1. Open VS Code
2. Click the **Extensions** icon on the left sidebar (it looks like four squares)
3. Search for **PlatformIO IDE**
4. Click **Install**
5. Wait for the installation to finish (this may take 2-3 minutes)
6. VS Code will show a PlatformIO home page when ready

### The Embedded Build Pipeline

Unlike running Python or JavaScript, C++ code for a microcontroller must go through several stages before it can execute on the chip. The tools below handle each stage. You do not need to configure them individually because PlatformIO manages all of them, but you will encounter their names and files in the project, so it helps to know what they are.

**Compiler.** The compiler reads your `.cpp` and `.h` source files and translates them into binary machine code (a `.uf2` file) that the RP2040 processor can execute. This is not optional; the chip cannot interpret C++ text. Compilation happens every time you click Build in PlatformIO.

**CMake (Build System).** A real firmware project has dozens of source files that depend on each other in specific ways. CMake is a build system that tracks these dependencies and tells the compiler which files to recompile when you make a change. You will see a file called `CMakeLists.txt` in the project. It is CMake's configuration file. You do not need to edit it; PlatformIO generates and manages it automatically.

**Arduino Framework.** The RP2040 chip's hardware is controlled through low-level memory-mapped registers. Writing to those registers directly is tedious and error-prone. The Arduino framework is a library layer that wraps these registers into simple, readable functions like `digitalWrite()`, `analogWrite()`, and `Serial.println()`. When you see these function calls in the firmware source, that is the Arduino framework at work. This project uses the [earlephilhower Arduino-Pico core](https://github.com/earlephilhower/arduino-pico), which is the community-maintained Arduino port for the Pico W.

**PlatformIO.** PlatformIO is a VS Code extension that ties all of the above together. It downloads the correct compiler for your target board, runs CMake, pulls in the Arduino framework, resolves library dependencies, compiles everything, and uploads the resulting binary to the Pico W over USB. Its configuration lives in `platformio.ini` at the project root, which specifies the target board (`rpipicow`), the framework (`arduino`), and any additional libraries.

> [!TIP]
> If you have used the Arduino IDE before, PlatformIO serves the same purpose but with better library management, multi-board support, and integration into a full code editor.

---

## 3. Configure Your Robot

The firmware already implements all the complex subsystems for you: BLE communication and pairing, mecanum kinematics, stepper and DC motor control, inter-core coordination, and safety monitoring. You do not need to write or modify any of that code. The only thing you need to do is tell the firmware about **your specific robot**: what it is called, what motor drivers it uses, and how the chassis is shaped. All of these settings live in **one file**: `src/project_config.h`. Open it in VS Code.

If your design requires mission-specific functionality (sensors, actuators, custom control logic, etc.), implement it as a separate **add-on module** alongside the existing code rather than editing the core firmware. The `profiles/` folder is a good model to follow: it adds behavior on top of the base system without changing it.

> [!WARNING]
> Do not modify files in `core/` or `drivers/` unless you fully understand what they do. These modules manage real-time motor control, BLE communication, and inter-core synchronization. A small change in the wrong place can break motor timing, drop Bluetooth connections, or introduce race conditions that are very difficult to debug.

### Step 3a: Set Your Robot's Name

Find this line near the top:

```cpp
#define DEVICE_NAME "TimberBot_RC"
```

Change `"TimberBot_RC"` to your own name. Include one of these keywords (case-insensitive) so the Flutter app shows your robot at the top of the scan list: `robot`, `rc`, `pico`, `mecanum`, `tank`, `meca`, `controller`.

**Example -- Good names:** `PicoBot_05`, `RC_Tank_1`, `MecaRover_A`, `Team7_ROBOT`, `warehouse_Pico_3`

**Example -- Bad names:** `Steve`, `TestDevice`, `Board3` (no keywords, these will appear at the bottom of the scan list)

### Step 3b: Set Your PIN

Find this line:

```cpp
#define BLE_PASSKEY 123456
```

Change `123456` to any 6-digit number you want. You will enter this PIN on your phone when pairing. The default (`123456`) works fine for testing.

### Step 3c: Select Your Motor Driver

Find the motor type section and **uncomment exactly one** line by removing the `//` at the start:

```cpp
// DC Motor drivers (uncomment ONE if using DC motors)
// #define MOTOR_DRIVER_DRV8871
// #define MOTOR_DRIVER_DRV8833
// #define MOTOR_DRIVER_L298N

// Stepper drivers (uncomment ONE if using steppers)
#define STEPPER_DRIVER_TMC2209       // ← this one is currently active
// #define STEPPER_DRIVER_A4988
// #define STEPPER_DRIVER_DRV8825
```

> [!IMPORTANT]
> Only **one** driver can be active at a time. If you uncomment two, the firmware will not compile.

**Not sure which driver you have?** Look at the chip on your motor driver board. The chip name is usually printed on the IC. If you are using the MechaPico MCB (the custom PCB for this course), use `TMC2209`.

### Step 3d: Measure Your Robot (Optional for First Test)

If you want accurate driving behavior, measure your robot's wheel radius and axle distances. See the [Configuration Reference](CONFIGURATION_REFERENCE.md#vehicle-geometry) for instructions. You can skip this for a first test; the defaults will work, but your robot may not drive perfectly straight.

---

## 4. Build the Firmware

Building compiles your C++ source code into a binary file that the Pico W can execute.

### Using the PlatformIO toolbar

1. Click the **checkmark icon** (✓) in the bottom toolbar of VS Code

### Using the terminal

1. Open the VS Code terminal: `View → Terminal` (or press `` Ctrl+` ``)
2. Run:
   ```
   pio run
   ```

### What to expect

A successful build ends with output like this:

```
Linking .pio/build/pico/firmware.elf
Building .pio/build/pico/firmware.uf2
====== [SUCCESS] Took 45.23s ======
```

> [!WARNING]
> **First build takes 3-5 minutes** because PlatformIO downloads the compiler, the Arduino core, and all libraries. Subsequent builds are much faster (10-30 seconds) because only changed files are recompiled.

If you see errors, check:
- Did you uncomment exactly one motor driver?
- Did you save `project_config.h` after editing?
- See [TROUBLESHOOTING.md](TROUBLESHOOTING.md#build--upload-issues) for common build errors

---

## 5. Upload to the Pico W

Uploading sends the compiled binary from your computer to the Pico W over USB.

1. Connect the Pico W to your computer with a USB cable
2. **If this is the very first upload** (the Pico has never had firmware before):
   - Hold the **BOOTSEL** button on the Pico W while plugging in the USB cable
   - The Pico appears as a USB drive on your computer
   - Release the button
3. Run:
   ```
   pio run -t upload
   ```

### What to expect

```
Uploading .pio/build/pico/firmware.uf2
====== [SUCCESS] Took 8.42s ======
```

The Pico W will reboot automatically after upload.

> [!NOTE]
> **What is BOOTSEL?** It is a physical button on the Pico W board. Holding it while plugging in USB puts the chip into "bootloader mode," which means it is waiting to receive new firmware. You only need to do this the first time. After that, PlatformIO can upload directly over USB without holding any buttons.

> [!TIP]
> **Upload fails with "port busy"?** Close the serial monitor first (if it is open), then retry the upload. The serial monitor and the uploader cannot use the USB port at the same time.

---

## 6. Verify It Works

The serial monitor shows text messages that the firmware prints as it initializes. This is how you confirm everything started correctly.

1. Run:
   ```
   pio run -t monitor
   ```
2. If the Pico was already running, press the **reset button** on the board (or unplug and replug USB) to restart it

### Expected boot sequence

You should see output like this:

```
[MCP23017] Initializing I2C: SDA=4, SCL=5, freq=400000
[MCP23017] Stepper MCP (0x20) initialized
[MCP23017] DC Motor MCP (0x21) initialized
[MotorManager] Registering 4 stepper motors
[MotorStepper] Motor 0 initialized (TMC2209)
[MotorStepper] Motor 1 initialized (TMC2209)
[MotorStepper] Motor 2 initialized (TMC2209)
[MotorStepper] Motor 3 initialized (TMC2209)
[BLE] Derived address: C0:AB:12:34:56:78 (hash: 0x1234)
[BLE] Device name: TimberBot_RC
[BLE] Advertising started - waiting for connection...
[Safety] Watchdog initialized (timeout: 2000ms)
```

### What each line means

| Line | What It Tells You |
|------|-------------------|
| `[MCP23017] ... initialized` | The I2C GPIO expander chips are communicating. If missing, check your I2C wiring (SDA, SCL). |
| `[MotorStepper] Motor X initialized` | Each motor registered successfully. If missing, check `project_config.h` motor type selection. |
| `[BLE] Derived address: ...` | The Pico computed a Bluetooth MAC address from your device name and PIN. |
| `[BLE] Advertising started` | The Pico is now broadcasting over Bluetooth, waiting for your phone to connect. **This is the most important line.** If it does not appear, BLE failed to initialize. |
| `[Safety] Watchdog initialized` | The safety system is active. If no commands arrive within 2 seconds, all motors will stop. |

> [!WARNING]
> If you see none of this output, check that your baud rate is **115200**. PlatformIO defaults to this, but if you changed it, serial output will appear as garbage characters.

---

## 7. Pair with the Flutter App

1. Make sure the Pico W is powered on and the serial monitor shows `[BLE] Advertising started`
2. Open the **RC Controller** Flutter app on your phone
3. Tap **Scan** on the connection screen
4. Your robot's name appears in the device list. Tap it.
5. Your phone will prompt for a **6-digit PIN**. Enter the `BLE_PASSKEY` from `project_config.h` (default: `123456`)
6. The serial monitor will show:
   ```
   [BLE] Client connected!
   [BLE] Requesting pairing...
   [BLE] First write received - connection fully established
   ```
7. The Pico W's built-in LED turns **ON** (solid) when connected

> [!TIP]
> **Robot not appearing in the scan list?** Check that the serial monitor shows `[BLE] Advertising started`. If it does, try moving your phone closer. Bluetooth range is typically 5-10 meters indoors.

---

## 8. Test Your Motors

With the app connected:

1. Select **Mecanum Drive** in the app (if not already selected)
2. Push the right joystick **gently forward**
3. All 4 motors should spin in the same direction (forward)

### First-motion checklist

| Check | What to Look For | If Wrong |
|-------|-------------------|----------|
| Any motors spinning? | At least one motor responds to joystick | Check wiring and serial output for errors |
| All 4 motors spinning? | All four respond | Check individual motor wiring |
| Correct direction? | All spin forward when joystick is pushed forward | Swap motor wires or flip direction in config |
| Smooth motion? | No stuttering or rattling | Check microstepping config matches your driver |

> [!NOTE]
> **What if nothing moves?** Open the serial monitor and check for error messages. The most common issue is that the motor driver type in `project_config.h` does not match the physical driver board. See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for a complete list of solutions.

---

## 9. What Just Happened?

Here is a summary of the entire pipeline you just set up:

```
  You edited project_config.h
       ↓
  PlatformIO compiled 26 source files into one binary (firmware.uf2)
       ↓
  The binary was uploaded to the Pico W's flash memory over USB
       ↓
  The Pico W booted and ran setup() on Core 0:
    - Initialized I2C bus and MCP23017 GPIO expanders
    - Registered 4 motors with the motor manager
    - Started BLE advertising with your device name
    - Started the safety watchdog timer
       ↓
  Core 1 started its own loop (setup1/loop1):
    - Polls target speeds every 500µs
    - Generates step pulses through the MCP23017
       ↓
  Your phone connected over BLE and paired with the PIN
       ↓
  The app sends JSON commands at ~50Hz:
    {"type":"control","vehicle":"mecanum","joystick":{"x":0.5,"y":1.0},"dial":0.0}
       ↓
  Core 0 parses the JSON → computes kinematics → writes wheel speeds
       ↓
  Core 1 reads wheel speeds → generates step pulses → motors spin
```

You now have a working robot. If something goes wrong later, see [TROUBLESHOOTING.md](TROUBLESHOOTING.md).

---

## Next Steps

Now that your robot drives, you have several paths forward:

| Goal | Document |
|------|----------|
| Understand every setting in `project_config.h` | [CONFIGURATION_REFERENCE.md](CONFIGURATION_REFERENCE.md) |
| Learn how the firmware code works internally | [ARCHITECTURE.md](ARCHITECTURE.md) |
| **Add your own motors, sensors, or profiles for the term project** | [EXTENDING_THE_FIRMWARE.md](EXTENDING_THE_FIRMWARE.md) |
| Debug a problem | [TROUBLESHOOTING.md](TROUBLESHOOTING.md) |
