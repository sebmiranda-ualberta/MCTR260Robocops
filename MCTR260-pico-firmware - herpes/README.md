# Raspberry Pi Pico W Robot Firmware

## What This Firmware Does

This firmware turns a Raspberry Pi Pico W into a **wireless robot controller**. You connect to it with a phone app over Bluetooth, and it translates your joystick movements into motor commands. It supports both DC motors (like the ones in toy cars) and stepper motors (precise motors that move in discrete steps), and it handles all the real-time control, safety, and communication automatically.

**In one sentence:** Phone app → Bluetooth → Pico W → motor drivers → wheels spin.

> [!IMPORTANT]
> **Hardware Platform:** This project is built around the **MECHA PICO** expansion board, a custom PCB designed for the MCTR 260 term project at the University of Alberta. All documentation, pin assignments, wiring diagrams, and code examples assume this specific board. If you are building from a bare Pico W, refer to the [Wiring Guide](doc/user_guide_documentation/WIRING_GUIDE.md) for the full connection list.

---

## How It Works

When you push the joystick in the Flutter app, here is what happens:

1. **App sends a JSON command** over Bluetooth Low Energy (BLE) to the Pico W
2. **Pico W parses the JSON** and extracts joystick position (x, y, rotation)
3. **Kinematics math runs** to convert your joystick input into 4 individual wheel speeds
4. **Motor drivers receive signals** (PWM for DC motors, step pulses for steppers)
5. **Safety watchdog monitors** the connection and stops everything if your phone disconnects

The Pico W has **two processor cores**. Core 0 handles Bluetooth and decision-making. Core 1 handles real-time stepper motor pulses. This separation ensures that Bluetooth processing never causes motor stuttering, and motor timing never blocks your Bluetooth connection.

---

## What You Need

### Hardware

| Component | Purpose | Notes |
|-----------|---------|-------|
| Raspberry Pi Pico W | The microcontroller brain | Must be the **W** variant (has Bluetooth) |
| Motor driver board(s) | Converts logic signals to motor power | See [Supported Motor Drivers](#supported-motor-drivers) below |
| Motors (up to 5 stepper + 2 DC) | Stepper and/or DC, depending on your build | Drive motors should be the same type |
| Power supply | Battery or bench supply | Voltage depends on your motors |
| USB cable | For uploading firmware and serial debugging | Micro-USB for Pico W |

### Software

| Tool | Purpose | Where to Get It |
|------|---------|-----------------|
| VS Code | Code editor | [code.visualstudio.com](https://code.visualstudio.com/) |
| PlatformIO extension | Builds and uploads firmware | Install from VS Code Extensions tab |
| RC Controller app | Phone app to drive the robot | Provided separately (Flutter) |

---

## Quick Start

> [!TIP]
> If this is your first time, read [GETTING_STARTED.md](doc/user_guide_documentation/GETTING_STARTED.md) for a detailed walkthrough of every step.

1. Open this project folder in VS Code with PlatformIO installed
2. Edit `src/project_config.h` to set your robot's name, PIN, and motor driver type
3. Build and upload:
   ```
   pio run -t upload
   ```
4. Open serial monitor to verify boot:
   ```
   pio run -t monitor
   ```
5. Open the Flutter app → scan for your robot → enter the PIN → drive!

---

## Project Structure

```
├── platformio.ini          # Build configuration (do not edit unless adding libraries)
├── src/
│   ├── project_config.h    # ← YOUR SETTINGS (device name, PIN, motor type, GPIO pins)
│   ├── main.cpp            # Entry point for both cores
│   ├── core/               # BLE communication, command parsing, motor management, safety
│   ├── drivers/            # Hardware drivers (MCP23017, kinematics math)
│   └── profiles/           # Motion profiles (mecanum, auxiliary motors)
├── doc/                    # Student documentation
├── resources/              # PCB design files and datasheets
├── LICENSE                 # MIT License
└── README.md               # You are here
```

**Key file:** `src/project_config.h` is the **only file** you need to edit for basic setup. Everything else is internal firmware code.

---

## Supported Motor Drivers

### DC Motor Drivers

| Driver | Motors per Chip | Pico Pins per Motor | Max Current | Best For |
|--------|:--------------:|:-------------------:|:-----------:|----------|
| DRV8871 | 1 | 2 PWM | 3.6A | High-power single motors |
| DRV8833 | 2 | 2 PWM | 1.5A | Small to medium robots |
| L298N | 2 | 3 (2 dir + 1 PWM) | 2A | Easy to source, common in kits |

### Stepper Motor Drivers

| Driver | Max Microsteps | Max Current | Noise Level | Best For |
|--------|:--------------:|:-----------:|:-----------:|----------|
| TMC2209 | 1/256 (internal) | 2.8A | Very quiet | Precision, quiet operation |
| A4988 | 1/16 | 2A | Moderate | Budget builds |
| DRV8825 | 1/32 | 2.5A | Moderate | Mid-range, higher resolution |

> [!IMPORTANT]
> You must uncomment **exactly one** motor driver in `project_config.h`. Uncommenting more than one causes a compile error.

### Which Driver Should I Pick?

- **Using the MechaPico MCB (custom PCB)?** → Use `TMC2209` (it is pre-wired on the board)
- **Building from scratch with DC motors?** → Start with `DRV8833` (cheap, 2 motors per chip)
- **Need maximum torque?** → Use `DRV8871` (highest current per channel)
- **Using stepper motors on a breadboard?** → Start with `A4988` (widely available, simple wiring)

---

## BLE Pairing

1. Power on your robot (plug in USB or connect battery)
2. Open the Flutter RC Controller app
3. Tap **Scan** on the connection screen
4. Select your robot's name from the list (e.g., `TimberBot_RC`)
5. Enter the 6-digit PIN (default: `123456`, set in `project_config.h`)
6. You are connected. Push the joystick to drive.

> [!TIP]
> **Robot not appearing in the scan list?** Include one of these keywords in your device name so the app shows it at the top: `robot`, `rc`, `pico`, `mecanum`, `tank`, `meca`, `controller`.

> [!WARNING]
> If you change `DEVICE_NAME` or `BLE_PASSKEY` in the firmware, your phone will see a "new device." Delete the old pairing from your phone's Bluetooth settings and re-pair.

---

## Feature Status

| Feature | Status | Notes |
|---------|:------:|-------|
| BLE Control | ✅ | BTstack GATT server with passkey pairing |
| DC Motors (M1-M4) | ✅ | DRV8871, DRV8833, L298N drivers via Pico GPIO |
| Stepper Motors (M1-M4) | ✅ | TMC2209, A4988, DRV8825 via MCP23017 |
| Motor 5 (Spare Stepper) | ✅ | Config-only: runs on Core 1 with full acceleration |
| DC Motors 3-4 (Auxiliary) | ✅ | Config-only: on/off direction via MCP23017 U6_2 |
| Mecanum Kinematics | ✅ | 4-wheel omnidirectional drive |
| Safety Watchdog | ✅ | Auto-stop on lost connection |
| Dual-Core | ✅ | Core 0: BLE/logic, Core 1: stepper pulses |
| Telemetry | ✅ | BLE notify characteristic |
| Encoders | ⏳ | Planned |
| PID Control | ⏳ | Planned |

---

## Where to Go Next

Read these documents **in order**. Each builds on the previous one:

| # | Document | Purpose | When to Read |
|:-:|----------|---------|-------------|
| 1 | **You are here** (README) | Orientation and overview | Right now |
| 2 | [GETTING_STARTED](doc/user_guide_documentation/GETTING_STARTED.md) | Build, upload, and pair for the first time | First time setting up |
| 3 | [CONFIGURATION_REFERENCE](doc/user_guide_documentation/CONFIGURATION_REFERENCE.md) | Every setting in `project_config.h` explained | Customizing your robot |
| 4 | [WIRING_GUIDE](doc/user_guide_documentation/WIRING_GUIDE.md) | Physical connections: Pico, MCP23017, drivers, motors | Wiring or debugging hardware |
| 5 | [ARCHITECTURE](doc/user_guide_documentation/ARCHITECTURE.md) | How the firmware works internally (dual-core, BLE, safety) | Understanding the code |
| 6 | [BLE_PROTOCOL](doc/user_guide_documentation/BLE_PROTOCOL.md) | BLE service/characteristic details, JSON schema, connection flow | Deep-dive into communication |
| 7 | [EXTENDING_THE_FIRMWARE](doc/user_guide_documentation/EXTENDING_THE_FIRMWARE.md) | Add your own motors, sensors, and profiles | **Term project: adding mission-specific code** |
| 8 | [TROUBLESHOOTING](doc/user_guide_documentation/TROUBLESHOOTING.md) | Common problems and solutions | When something breaks |
