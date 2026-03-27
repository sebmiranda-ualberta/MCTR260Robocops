# Configuration Reference

All configuration for your Pico W robot lives in **one file**: `src/project_config.h`. This reference explains every setting, what it does, and how to choose the right values for your robot.

> [!IMPORTANT]
> After changing `project_config.h`, you must rebuild and re-upload the firmware. These are **compile-time settings**, meaning they are baked into the binary during compilation. Changing the file alone does nothing until you upload again.

---

## Table of Contents

1. [How Configuration Works](#how-configuration-works)
2. [Device Identity](#device-identity)
3. [Motor Type Selection](#motor-type-selection)
4. [Motor Driver Comparison](#motor-driver-comparison)
5. [Motion Profiles](#motion-profiles)
6. [Vehicle Geometry](#vehicle-geometry)
7. [GPIO Pin Assignments](#gpio-pin-assignments)
8. [Stepper Parameters](#stepper-parameters)
9. [Timing Parameters](#timing-parameters)
10. [Microstepping Reference](#microstepping-reference)
11. [Quick Configuration Examples](#quick-configuration-examples)

---

## How Configuration Works

### What is a `#define`?

In C/C++, `#define` is a preprocessor directive that creates a named constant. When you write:

```cpp
#define DEVICE_NAME "PicoBot_05"
```

the compiler replaces every occurrence of `DEVICE_NAME` in the code with `"PicoBot_05"` before compilation begins. Think of it like "find and replace" in a word processor, but it happens automatically before the code is compiled.

### Uncommenting a Line

Some settings are "commented out" (disabled) with `//` at the start:

```cpp
// #define MOTOR_DRIVER_DRV8871      // ← disabled (the // makes the compiler ignore this line)
#define STEPPER_DRIVER_TMC2209       // ← active (no // at the start)
```

To enable a setting, remove the `//`. To disable it, add `//` at the start. Only **one** motor driver can be active at a time.

---

## Device Identity

```cpp
#define DEVICE_NAME "TimberBot_RC"
#define BLE_PASSKEY 123456
```

| Setting | Purpose | Default | Notes |
|---------|---------|---------|-------|
| `DEVICE_NAME` | BLE advertised name | `"TimberBot_RC"` | This is the name that appears in the app's scan list |
| `BLE_PASSKEY` | 6-digit PIN for pairing | `123456` | Must match what you enter on your phone |

### Device Name Keywords

> [!IMPORTANT]
> The Flutter app **pushes robots to the top of the scan list** if the device name contains any of these keywords (case-insensitive):
>
> `robot` · `rc` · `pico` · `mecanum` · `tank` · `meca` · `controller`
>
> **Pick a name that includes at least one keyword** so your robot does not get buried under other Bluetooth devices (headphones, smartwatches, etc.).

**Good names:** `PicoBot_05`, `RC_Tank_1`, `MecaRover`, `MyRobotController`

**Bad names:** `Steve`, `TestDevice`, `Board3` (these will appear at the bottom of the list)

### How BLE Identity Works

The Pico W **derives its Bluetooth MAC address** (its unique identifier on the wireless network) from the device name and passkey:

```
CRC16(DEVICE_NAME + BLE_PASSKEY) → unique MAC address
```

This means:
- **Change the name or PIN → the MAC address changes**
- Your phone will see a "new device" and you must re-pair
- This is intentional: it allows running multiple Pico robots with unique identities
- The address is deterministic (same name + PIN always produces the same address)

> [!WARNING]
> If you rename your robot and cannot connect, delete the old pairing from your phone's Bluetooth settings first, then scan and pair again.

---

## Motor Type Selection

Uncomment **exactly one** motor driver define in `project_config.h`:

```cpp
// DC Motor drivers (uncomment ONE if using DC motors):
// #define MOTOR_DRIVER_DRV8871
// #define MOTOR_DRIVER_DRV8833
// #define MOTOR_DRIVER_L298N

// Stepper drivers (uncomment ONE if using steppers):
#define STEPPER_DRIVER_TMC2209
// #define STEPPER_DRIVER_A4988
// #define STEPPER_DRIVER_DRV8825
```

> [!CAUTION]
> Enabling more than one driver simultaneously will cause a compile error. Only one DC or one stepper driver may be active at a time.

### How to Pick a Driver

Not sure which driver to use? Follow this decision tree:

```
Are you using the MechaPico MCB (custom PCB)?
├── Yes → STEPPER_DRIVER_TMC2209 (pre-wired on the board)
└── No
    ├── Are your motors DC (continuous rotation) or stepper (discrete steps)?
    │   ├── DC motors
    │   │   ├── Need > 2A per motor? → MOTOR_DRIVER_DRV8871
    │   │   ├── Need 2 motors per chip? → MOTOR_DRIVER_DRV8833
    │   │   └── Using an L298N board from a kit? → MOTOR_DRIVER_L298N
    │   └── Stepper motors
    │       ├── Want quiet operation? → STEPPER_DRIVER_TMC2209
    │       ├── Budget build? → STEPPER_DRIVER_A4988
    │       └── Need > 1/16 microstepping? → STEPPER_DRIVER_DRV8825
    └──
```

---

## Motor Driver Comparison

### What is an H-Bridge?

An H-bridge is a circuit that allows you to control the direction of a motor. It gets its name from the "H" shape of the circuit diagram. By switching different combinations of four transistors, the H-bridge can send current through the motor in either direction (forward or reverse), or stop it entirely.

All three DC motor drivers below contain H-bridge circuits. The difference between them is how many motors they can drive, how much current they can handle, and how many control pins they need.

### DC Motor Drivers

| Feature | DRV8871 | DRV8833 | L298N |
|---------|:-------:|:-------:|:-----:|
| **Motors per IC** | 1 | 2 | 2 |
| **ICs needed for 4 motors** | 4 | 2 | 2 |
| **Pico pins per motor** | 2 PWM | 2 PWM | 3 (2 dir + 1 PWM) |
| **Total Pico pins for 4 motors** | 8 | 8 | 12 |
| **Max current (per channel)** | 3.6A | 1.5A | 2A |
| **Max voltage** | 45V | 10.8V | 46V |
| **Built-in current limit** | ✅ | ✅ | ❌ |
| **Heat dissipation** | Low | Low | High (needs big heatsink) |
| **Best for** | High-power motors | Small/medium robots | Legacy, easy to source |

**Pin control pattern per driver:**

```
DRV8871 / DRV8833:          L298N:
  Pico GPIO → IN1 (PWM)      Pico GPIO → ENA (PWM for speed)
  Pico GPIO → IN2 (PWM)      Pico GPIO → IN1 (direction)
                              Pico GPIO → IN2 (direction)
```

### Stepper Motor Drivers

| Feature | TMC2209 | A4988 | DRV8825 |
|---------|:-------:|:-----:|:-------:|
| **Interface** | Step/Dir | Step/Dir | Step/Dir |
| **Max microsteps** | 1/256 (internal) | 1/16 | 1/32 |
| **HW microstep pins** | MS1, MS2 | MS1, MS2, MS3 | MS1, MS2, MS3 |
| **Max current** | 2.8A RMS | 2A | 2.5A |
| **StealthChop (silent mode)** | ✅ | ❌ | ❌ |
| **Stall detection** | ✅ (via UART) | ❌ | ❌ |
| **Noise level** | Very quiet | Moderate | Moderate |
| **Best for** | Precision, quiet ops | Budget builds | Mid-range, high resolution |

> [!TIP]
> On the MechaPico MCB, stepper control signals go through an MCP23017 I2C expander. You do **not** wire STEP/DIR to Pico GPIO pins directly. See the [Wiring Guide](WIRING_GUIDE.md).

---

## Motion Profiles

```cpp
#define MOTION_PROFILE_MECANUM     // 4-wheel omnidirectional
// #define MOTION_PROFILE_DIRECT   // Raw per-motor control via aux channels
```

### Vehicle Types (Sent by the Flutter App)

The app sends a `"vehicle"` field in each JSON command. The firmware uses this to decide how to interpret the joystick, dial, and slider inputs:

| JSON `"vehicle"` Value | App Display Name | Left Control | Right Control |
|----------------------|-----------------|--------------|---------------|
| `"mecanum"` | Mecanum Drive | Dial (rotation ω) | Joystick (vx, vy strafe) |
| `"fourwheel"` | Four Wheel Drive | Dial (steering) | Slider (throttle) |
| `"tracked"` | Tracked Drive | Slider (left track) | Slider (right track) |
| `"dual"` | Dual Joystick | Joystick (left) | Joystick (right) |

### Firmware Motion Profiles

| Profile | What It Does | Vehicle Types |
|---------|-------------|---------------|
| **Mecanum** | Runs inverse kinematics to compute 4 independent wheel speeds from vx, vy, ω | `mecanum` (primary), adaptable to others |
| **Direct** | Maps aux channels directly to individual motors (no kinematics) | Any (raw control) |

> [!TIP]
> Want to create your own motion profile for a gripper, turret, or custom actuator? See [EXTENDING_THE_FIRMWARE.md](EXTENDING_THE_FIRMWARE.md) for a step-by-step guide.

> [!CAUTION]
> **Settings you should NOT change** unless you understand the consequences:
> - `PIN_I2C_SDA` / `PIN_I2C_SCL`: Hardwired on the MECHA PICO PCB. Changing these breaks all MCP23017 communication.
> - `MCP_STEPPER_I2C_ADDR` / `MCP_DCMOTOR_I2C_ADDR`: Set by hardware address pins on the board. Changing these in firmware without resoldering will cause I2C init failure.
> - `STEPPER_PULSE_INTERVAL_US`: Below ~200µs, I2C writes cannot complete in time and step pulses will be lost.

---

## Vehicle Geometry

These measurements feed into the mecanum kinematics calculations. Accuracy directly affects how straight your robot drives and how well it strafes.

### What to Measure

```
    ┌──── TRACK_WIDTH ────┐
    │                     │
   [FL]───────────────[FR]    ─┐
    │                     │    │  WHEELBASE
    │      (center)       │    │
    │                     │    │
   [BL]───────────────[BR]    ─┘
```

| Setting | What to Measure | How to Measure | Default | Unit |
|---------|----------------|----------------|---------|------|
| `WHEEL_RADIUS_MM` | Radius of one wheel | Measure the outer diameter with calipers, divide by 2 | `50.0f` | mm |
| `WHEELBASE_HALF_MM` | Center to front axle | Measure front-to-rear axle distance, divide by 2 | `100.0f` | mm |
| `TRACK_WIDTH_HALF_MM` | Center to left wheel | Measure left-to-right wheel distance on one axle, divide by 2 | `100.0f` | mm |

> [!TIP]
> All three measurements use "half" values (divide by 2) because the kinematics equations are written relative to the robot's center point.

---

## GPIO Pin Assignments

### DC Motors: DRV8871

| Motor | GPIO A (PWM) | GPIO B (PWM) |
|-------|:------------:|:------------:|
| Front-Left | GP2 | GP3 |
| Front-Right | GP4 | GP5 |
| Back-Left | GP6 | GP7 |
| Back-Right | GP8 | GP9 |

### DC Motors: DRV8833

Two chips, each driving two motors:

| Motor | GPIO IN1 (PWM) | GPIO IN2 (PWM) |
|-------|:--------------:|:--------------:|
| Front-Left (Chip 1 A) | GP2 | GP3 |
| Front-Right (Chip 1 B) | GP4 | GP5 |
| Back-Left (Chip 2 A) | GP6 | GP7 |
| Back-Right (Chip 2 B) | GP8 | GP9 |

### DC Motors: L298N

Two modules, three pins per motor:

| Motor | Enable (PWM) | IN1 (Dir) | IN2 (Dir) |
|-------|:------------:|:---------:|:---------:|
| Front-Left (Mod 1) | GP2 | GP3 | GP4 |
| Front-Right (Mod 1) | GP5 | GP6 | GP7 |
| Back-Left (Mod 2) | GP8 | GP9 | GP10 |
| Back-Right (Mod 2) | GP11 | GP12 | GP13 |

### Stepper Motors: via MCP23017

Stepper motor STEP/DIR signals are **not** connected to Pico GPIO pins. They go through the MCP23017 I2C GPIO expander. The Pico only connects to the I2C bus:

| Signal | Pico GPIO | Notes |
|--------|:---------:|-------|
| I2C SDA | GP4 | I2C0 data line |
| I2C SCL | GP5 | I2C0 clock line |
| I2C INT | GP22 | Interrupt (optional, currently unused) |

For the full MCP23017 pin mapping (which motor connects to which MCP23017 pin), see [WIRING_GUIDE.md](WIRING_GUIDE.md).

---

## Stepper Parameters

| Setting | Purpose | Default | Notes |
|---------|---------|---------|-------|
| `STEPPER_STEPS_PER_REV` | Base steps per revolution | `200` | Standard 1.8° motors have 200 steps |
| `STEPPER_MICROSTEPPING` | Microstep divisor | `8` | TMC2209 default is 8 |
| `STEPPER_MAX_SPEED` | Maximum step rate | `4000.0` steps/sec | Higher values need faster I2C |
| `STEPPER_ACCELERATION` | Ramp rate | `8000.0` steps/sec² | Prevents missed steps at startup |
| `STEPPER_PULSE_INTERVAL_US` | Core 1 loop period | `500` µs | 500µs = 2kHz update rate |
| `STEPPER_SPEED_DEADZONE` | Minimum active speed | `10.0` steps/sec | Below this, motor is stopped |

### Effective Steps per Revolution

```
Effective steps = STEPPER_STEPS_PER_REV × STEPPER_MICROSTEPPING
Example: 200 × 8 = 1,600 effective steps per revolution
```

This means the motor must execute 1,600 step pulses to complete one full rotation. Higher microstepping gives smoother motion but requires faster pulse rates.

### Speed vs. Pulse Interval

```
Max speed at full resolution = 1,000,000 ÷ (STEPPER_PULSE_INTERVAL_US × 2)
Default: 1,000,000 ÷ (500 × 2) = 1,000 steps/sec per motor
```

The "× 2" factor exists because each step pulse needs a HIGH and a LOW phase. If you need higher speeds, reduce `STEPPER_PULSE_INTERVAL_US`, but keep it above ~200µs or I2C writes will not complete in time.

---

## Timing Parameters

| Setting | Purpose | Default | Notes |
|---------|---------|---------|-------|
| `MOTOR_UPDATE_INTERVAL_MS` | DC motor update rate | `20` ms (50Hz) | Lower = smoother but more CPU usage |
| `SAFETY_TIMEOUT_MS` | Emergency stop timeout | `2000` ms | Stops motors if no command in 2 seconds |
| `TELEMETRY_INTERVAL_MS` | BLE telemetry rate | `500` ms | How often battery/speed data is sent to app |

> [!IMPORTANT]
> `SAFETY_TIMEOUT_MS` must be **larger** than the app's heartbeat interval (typically 1000ms). If set too low, the robot will stutter during normal BLE latency spikes. If set too high, the robot will keep moving too long after a disconnect.

---

## Microstepping Reference

### What is Microstepping?

A standard stepper motor moves in discrete "steps" (typically 200 per revolution, or 1.8° per step). Microstepping divides each step into smaller sub-steps. For example, 1/8 microstepping divides each 1.8° step into eight 0.225° sub-steps, giving 1,600 steps per revolution.

**Why use microstepping?**
- **Smoother motion:** Fewer vibrations, less noise
- **Higher resolution:** More precise positioning
- **Trade-off:** Requires faster pulse rates (more steps per second for the same rotation speed)

### TMC2209 Truth Table

| MS1 | MS2 | Microsteps | Effective steps/rev (200-step motor) |
|:---:|:---:|:----------:|:------------------------------------:|
| 0 | 0 | 8 | 1,600 |
| 1 | 0 | 2 | 400 |
| 0 | 1 | 4 | 800 |
| 1 | 1 | 16 | 3,200 |

Default on MechaPico MCB: **MS1=0, MS2=0 → 8 microsteps** (set via MCP23017 GPA4/GPA5)

### A4988 / DRV8825 Truth Table

| MS1 | MS2 | MS3 | Microsteps | Effective steps/rev |
|:---:|:---:|:---:|:----------:|:-------------------:|
| 0 | 0 | 0 | Full step (1) | 200 |
| 1 | 0 | 0 | Half step (1/2) | 400 |
| 0 | 1 | 0 | 1/4 step | 800 |
| 1 | 1 | 0 | 1/8 step | 1,600 |
| 1 | 1 | 1 | 1/16 step | 3,200 |

> [!WARNING]
> TMC2209 and A4988 have **different** MS1/MS2 truth tables! A "0, 0" setting gives **8 microsteps** on TMC2209 but **full steps** on A4988. Make sure `STEPPER_MICROSTEPPING` in your config matches what your hardware actually does. Wrong microstepping = wrong robot speed.

---

## Quick Configuration Examples

### Example 1: DC Robot with DRV8871 Drivers

```cpp
#define DEVICE_NAME "DC_Robot_01"
#define BLE_PASSKEY 654321
#define MOTOR_DRIVER_DRV8871
#define MOTION_PROFILE_MECANUM
#define WHEEL_RADIUS_MM 30.0f
#define WHEELBASE_HALF_MM 75.0f
#define TRACK_WIDTH_HALF_MM 80.0f
```

### Example 2: Stepper Robot with TMC2209 (MechaPico MCB)

```cpp
#define DEVICE_NAME "MecaPico_01"
#define BLE_PASSKEY 111111
#define STEPPER_DRIVER_TMC2209
#define MOTION_PROFILE_MECANUM
#define STEPPER_MICROSTEPPING 8
#define STEPPER_MAX_SPEED 4000.0f
#define STEPPER_ACCELERATION 8000.0f
```

### Example 3: Budget Stepper with A4988

```cpp
#define DEVICE_NAME "RC_Tank_01"
#define BLE_PASSKEY 999999
#define STEPPER_DRIVER_A4988
#define MOTION_PROFILE_MECANUM
#define STEPPER_MICROSTEPPING 1      // Full step for max torque
#define STEPPER_MAX_SPEED 800.0f     // Lower for full-step mode
#define STEPPER_ACCELERATION 2000.0f
```
