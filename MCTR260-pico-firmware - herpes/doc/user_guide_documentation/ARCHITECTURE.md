# Firmware Architecture

This document explains **how the firmware code is organized** and how the different parts work together. You do not need to understand this to use the firmware, but you will need it if you want to modify the code or debug complex issues.

> **Hardware Platform:** The Raspberry Pi Pico W is mounted on a custom-designed expansion PCB called the **MECHA PICO** board, which is provided to students as part of the MCTR 260 term project material components. All project documentation, pin assignments, I2C bus configurations, and code implementation in this repository are written specifically for the MECHA PICO board's circuit topology and connector layout. A bare Pico W will not match the pin mappings or peripheral connections described here.

---

## Table of Contents

1. [What This Firmware Does](#what-this-firmware-does)
2. [Key Concepts](#key-concepts)
3. [System Architecture](#system-architecture)
4. [Data Flow: Joystick to Motor](#data-flow-joystick-to-motor)
5. [Command Lifecycle](#command-lifecycle)
6. [BLE System](#ble-system)
7. [Motor Hierarchy](#motor-hierarchy)
8. [Inter-Core Communication](#inter-core-communication)
9. [File Structure](#file-structure)

---

## What This Firmware Does

When you push the joystick in the Flutter app, a JSON command travels over Bluetooth to the Pico W, gets parsed into motor speeds, and crosses from Core 0 to Core 1 via shared memory. Core 1 then generates precisely-timed step pulses through an MCP23017 I2C GPIO expander.

**The problem it solves:** The RP2040 chip has two processor cores, and we use both. Core 0 handles the "smart" work (Bluetooth, parsing, kinematics, safety). Core 1 handles the "fast" work (generating step pulses every 500 microseconds). This separation ensures that Bluetooth processing never causes stepper jitter, and stepper timing never blocks Bluetooth.

---

## Key Concepts

Read this section first if any of these terms are unfamiliar.

### What is BLE (Bluetooth Low Energy)?

BLE is a wireless communication protocol designed for low-power devices. Unlike classic Bluetooth (used for audio streaming), BLE sends small packets of data infrequently, which saves battery. Our firmware uses BLE to receive joystick commands from the Flutter app at about 50 times per second.

The Pico W acts as a **GATT server**, which means it advertises its presence and waits for a client (your phone) to connect and write data. "GATT" stands for Generic Attribute Profile. It defines how data is organized into "services" and "characteristics." Think of a service as a folder and a characteristic as a file inside that folder. Our firmware has one service with two characteristics: one for receiving commands (the phone writes to it) and one for sending telemetry (the phone reads from it).

### What is a Dual-Core Processor?

The RP2040 chip inside the Pico W has **two independent CPU cores** (Core 0 and Core 1). Each core can run its own code simultaneously, like two workers in a factory doing different jobs at the same time. In Arduino on the Pico W:

- **Core 0** runs `setup()` and `loop()` (the standard Arduino functions)
- **Core 1** runs `setup1()` and `loop1()` (Pico-specific extensions)

We use this to separate Bluetooth communication (Core 0) from real-time motor control (Core 1). If both tasks ran on the same core, Bluetooth connection events (which can take several milliseconds) would cause visible motor stuttering.

### What is I2C?

I2C (pronounced "I-squared-C") is a communication protocol that uses just **two wires** (SDA for data, SCL for clock) to connect multiple devices. Each device on the bus has a unique address (like a mailing address). The Pico W is the "master" that initiates communication, and the MCP23017 chips are "slaves" that respond.

Our firmware uses I2C at 400 kHz ("Fast Mode") to communicate with two MCP23017 GPIO expander chips. This is how the Pico W controls stepper motors even though the motor signals are not directly connected to Pico GPIO pins.

### What is a Mutex?

A mutex (short for "mutual exclusion") is a lock that prevents two processor cores from reading/writing the same memory at the same time. Imagine a bathroom with one key: only one person can use it at a time. If Core 0 is writing new motor speeds to shared memory, Core 1 must wait until Core 0 finishes (and vice versa).

Without a mutex, one core might read a speed value while the other core is halfway through writing it, resulting in a corrupted number (a "race condition").

### What is a GPIO Pin?

GPIO stands for "General Purpose Input/Output." These are the physical pins on the Pico W board that you can use to send or receive electrical signals. Each pin has a number (GP0, GP1, GP2, etc.) and can be configured as either an input (reading a sensor) or an output (driving a motor).

---

## System Architecture

```mermaid
flowchart TB
    subgraph App["Flutter App (Phone)"]
        JS["Joystick / Controls"]
    end

    subgraph Core0["Core 0: BLE + Logic"]
        BLE["BTstack BLE\nGATT Server"]
        CP["command_parser\nJSON → struct"]
        PM["profile_mecanum\nKinematics math"]
        SAF["safety\nWatchdog timer"]
    end

    subgraph Core1["Core 1: Real-Time Motor Control"]
        SS["simple_stepper\n500µs loop"]
        MCP["MCP23017 I2C\nBatch port writes"]
    end

    subgraph HW["Hardware"]
        M1["Motor 1 FL"]
        M2["Motor 2 FR"]
        M3["Motor 3 BL"]
        M4["Motor 4 BR"]
    end

    JS -->|"BLE JSON"| BLE
    BLE --> CP
    CP --> PM
    PM -->|"mutex_try_enter\ng_targetSpeeds"| SS
    SAF -.->|"timeout → stop"| SS
    SS --> MCP
    MCP --> M1
    MCP --> M2
    MCP --> M3
    MCP --> M4
```

- **Solid arrows** show the normal data flow (commands flowing down from app to motors)
- **Dashed arrow** shows the safety watchdog, which runs independently and triggers an emergency stop if no commands arrive within 2 seconds
- The **mutex boundary** between Core 0 and Core 1 is where shared memory is protected by a lock
- **BLE JSON** means the phone sends commands as text in JSON format over Bluetooth

---

## Data Flow: Joystick to Motor

This is the path of a single joystick command from your finger to the motor:

```mermaid
sequenceDiagram
    participant App as Flutter App
    participant BLE as BTstack BLE
    participant CP as CommandParser
    participant PM as ProfileMecanum
    participant MK as MecanumKinematics
    participant MX as Mutex
    participant SS as SimpleStepper
    participant IO as MCP23017

    App->>BLE: JSON write to characteristic
    BLE->>CP: on_write callback
    CP->>CP: deserializeJson()
    CP->>PM: profile_mecanum_apply(cmd)
    PM->>MK: mecanum_calculate(vx, vy, omega)
    MK-->>PM: WheelSpeeds (4 floats)
    PM->>MX: mutex_try_enter (non-blocking)
    MX-->>PM: success/fail
    PM->>MX: write g_targetSpeeds
    Note over SS: Core 1 loop (every 500µs)
    SS->>MX: mutex_try_enter (non-blocking)
    MX-->>SS: read g_targetSpeeds (if lock acquired)
    SS->>IO: setPortB(dir + step mask)
    IO->>IO: I2C write to stepper drivers
```

**Key timing numbers:**

| What | How Long | Why It Matters |
|------|----------|----------------|
| BLE → motor total latency | ~20ms | Dominated by BLE connection interval (not our code) |
| Core 1 loop period | 500µs (2,000 Hz) | This is how often stepper pulses can be generated |
| I2C batch write (all 4 motors) | ~100µs | Only 2 I2C writes needed thanks to batching optimization |
| Mutex hold time | ~1µs | Core 0 holds the lock only while writing 4 float values |

---

## Command Lifecycle

### Vehicle Types

The Flutter app sends a `"vehicle"` field in each JSON command. This tells the firmware which kinematics model to use:

| JSON Value | App Display Name | Left Control | Right Control | Status |
|-----------|----------|-------------|---------------|--------|
| `"mecanum"` | Mecanum Drive | Dial (rotation ω) | Joystick (vx, vy strafe) | ✅ Active |
| `"fourwheel"` | Four Wheel Drive | Dial (steering) | Slider (throttle) | ⏳ Parsed but not routed |
| `"tracked"` | Tracked Drive | Slider (left track) | Slider (right track) | ⏳ Parsed but not routed |
| `"dual"` | Dual Joystick | Joystick (left) | Joystick (right) | ⏳ Parsed but not routed |

> [!NOTE]
> Only `"mecanum"` is currently routed to a motion profile. The other three vehicle types are parsed by `command_parser.cpp` but silently ignored in `main.cpp`. To add support, implement a new profile and add routing in `onBleCommand()`.

### Processing Flow

```mermaid
flowchart LR
    A["JSON received\nvia BLE write"] --> B["Parse with\nArduinoJson"]
    B --> C{"type?"}
    C -->|"control"| D["Route by\nvehicle type"]
    C -->|"heartbeat"| E["Feed safety\nwatchdog only"]
    D --> F["mecanum_calculate\n4 wheel speeds"]
    F --> G["Scale to motor\nunits"]
    G --> H{"Motor type?"}
    H -->|"DC"| I["motor_set_pwm\n±255 PWM"]
    H -->|"Stepper"| J["mutex → Core 1\n±steps/sec"]
```

**Two command types:**

- **`"control"`:** Contains joystick/dial/slider data. The firmware computes kinematics and drives the motors.
- **`"heartbeat"`:** Contains no motor data. Its only purpose is to tell the safety watchdog "I'm still here, don't stop the motors." The app sends heartbeats even when the joystick is centered and there is no user input.

---

## BLE System

For the complete BLE specification (UUIDs, security, MAC derivation, boot sequence, messaging system, JSON schemas, data structures, framing, troubleshooting), see [BLE_PROTOCOL.md](BLE_PROTOCOL.md).

This section provides a quick reference. The full document has all the detail.

### Service and Characteristic UUIDs

| Role | UUID | Direction |
|------|------|-----------|
| **Service** | `4fafc201-1fb5-459e-8fcc-c5c9c331914b` | - |
| **Control** | `beb5483e-36e1-4688-b7f5-ea07361b26a8` | App → Firmware (Write) |
| **Telemetry** | `beb5483f-36e1-4688-b7f5-ea07361b26a8` | Firmware → App (Notify) |

### Command Summary

| Type | Direction | Rate | Purpose |
|------|-----------|------|---------|
| `"control"` | App → Firmware | 50Hz | Joystick/dial/slider → motor speeds |
| `"heartbeat"` | App → Firmware | 1Hz | Safety watchdog keep-alive |
| `"telemetry"` | Firmware → App | Periodic | Battery, RSSI, status |
| `"message"` | Firmware → App | Event-driven | Warnings, errors, action prompts |
| `"message_response"` | App → Firmware | On user action | Response to message action buttons |

### Safety

| Feature | Behavior |
|---------|----------|
| **Heartbeat watchdog** | Motors stop if no command or heartbeat within `SAFETY_TIMEOUT_MS` (default 2000ms) |
| **BLE disconnect** | Motors stop immediately |
| **Dual-layer** | Both mechanisms are independent backups |

---

## Motor Hierarchy

```mermaid
classDiagram
    class MotorBase {
        <<abstract>>
        +init() bool
        +stop()
        +update(dtSec)
        +getIndex() uint8_t
        +isEnabled() bool
        +getType() MotorType
    }

    class MotorDC {
        -cfg_ MotorDCConfig
        -targetPWM_ int16_t
        -currentPWM_ int16_t
        +setTarget(pwm)
        +getTarget() int16_t
        -applyPWM(pwm)
    }

    class MotorStepper {
        -cfg_ MotorStepperConfig
        -currentSpeed_ float
        -targetSpeed_ float
        +setTargetSpeed(speed)
        +setTargetPosition(pos)
        -calculateStepInterval()
        -generateStep()
    }

    MotorBase <|-- MotorDC
    MotorBase <|-- MotorStepper

    class SimpleStepper {
        <<module>>
        +simple_stepper_init()
        +simple_stepper_update()
        +simple_stepper_set_speed(motor, speed)
    }

    note for SimpleStepper "Flat C API on Core 1\nBypasses class hierarchy\nfor I2C batching performance"
```

**How to read this diagram:**

- `MotorBase` is an **abstract class** (a template). It defines what every motor must be able to do (init, stop, update) but does not implement the details.
- `MotorDC` and `MotorStepper` **inherit** from `MotorBase`. They provide the actual implementation for each motor type.
- `SimpleStepper` is a **separate module** (not a class). It runs on Core 1 and bypasses the class hierarchy for performance reasons. See "Why two stepper implementations?" below.

### Why Two Stepper Implementations?

| | MotorStepper (class) | simple_stepper (C module) |
|--|---|---|
| **Runs on** | Core 0 | Core 1 |
| **I2C writes per update** | 2 per motor (8 total for 4 motors) | 2 for ALL motors combined |
| **Features** | Trapezoidal acceleration, position mode | Velocity only, accumulator timing |
| **Currently used for** | Future position control (not active) | Real-time pulse generation (active) |

**Why does this matter?** Each I2C write takes about 50 microseconds. If we wrote to each motor individually (8 writes), that would take 400 microseconds, leaving almost no time in the 500-microsecond loop for anything else. The batch approach (2 writes total) takes only about 100 microseconds, leaving plenty of margin.

---

## Inter-Core Communication

The two cores share data through global variables protected by a mutex:

```
Core 0 (BLE + Logic)                Core 1 (Stepper Pulses)
┌────────────────────────┐          ┌──────────────────┐
│                        │          │                  │
│  1. Compute 4 wheel    │          │  3. Read speeds  │
│     speeds             │          │     from shared  │
│                        │          │     memory       │
│                        │          │                  │
│  2. Write to:          │          │  4. Generate     │
│     g_targetSpeeds[4]  │          │     step pulses  │
│     g_speedsUpdated    │          │     via MCP23017 │
│     g_emergencyStop    │          │                  │
│                        │          │                  │
│  Uses: mutex_          │          │  Uses: mutex_    │
│  try_enter()           │◄────────►│  try_enter()     │
│  (non-blocking)        │ g_speed  │  (non-blocking)  │
│                        │  Mutex   │                  │
└────────────────────────┘          └──────────────────┘
```

### Why does Core 0 use `mutex_try_enter()` (non-blocking)?

If Core 1 holds the mutex (it is in the middle of an I2C write), Core 0 **must not wait**. The Bluetooth stack (BTstack) runs its own event loop on Core 0, and if that loop stalls for even a few milliseconds, Bluetooth connections can time out and drop. Instead, Core 0 tries to acquire the lock, and if it fails, it simply **skips that update**. At 50 commands per second, dropping one frame is invisible to the user.

### Why does Core 1 also use `mutex_try_enter()` (non-blocking)?

Core 1's primary job is generating step pulses at precise 500-microsecond intervals. If it blocked waiting for Core 0, a long BLE callback could cause Core 1 to miss its timing window and produce motor jitter. Instead, Core 1 tries the lock: if it gets it, it reads new speeds; if not, it immediately proceeds with `simple_stepper_update()` using the previous speeds. At 2,000 iterations per second, skipping one read is invisible.

> [!NOTE]
> The `mutex_enter_blocking()` calls in `main.cpp` are only used by Core 0 for **safety-critical** events (BLE disconnect and watchdog timeout) where the emergency stop flag **must** reach Core 1 without being skipped.

---

## File Structure

```
firmwares/pico/src/
├── main.cpp                  # Entry point: setup/loop for both cores
├── project_config.h          # ← USER SETTINGS (device name, motors, GPIO)
│
├── core/
│   ├── ble_controller.cpp/h  # BTstack BLE GATT server + MAC derivation
│   ├── ble_config.h          # UUID constants (must match ESP32 + Flutter app)
│   ├── command_parser.cpp/h  # JSON → control_command_t struct
│   ├── command_packet.h      # Shared command struct definition
│   ├── motor_base.h          # Abstract motor interface
│   ├── motor_dc.cpp/h        # DC motor: PWM via H-bridge drivers
│   ├── motor_stepper.cpp/h   # Stepper: trapezoidal accel via MCP23017
│   ├── motor_manager.cpp/h   # Motor registry, init, microstepping config
│   ├── safety.cpp/h          # Watchdog: stop motors on lost connection
│   └── simple_stepper.cpp/h  # Core 1 real-time stepper pulse generator
│
├── drivers/
│   ├── mcp23017.cpp/h        # MCP23017 I2C GPIO expander driver
│   └── mecanum_kinematics.cpp/h  # Inverse kinematics math
│
└── profiles/
    └── profile_mecanum.cpp/h # Joystick → kinematics → motor speeds
```
