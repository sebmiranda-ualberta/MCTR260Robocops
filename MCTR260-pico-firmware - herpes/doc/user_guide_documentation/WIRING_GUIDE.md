# Wiring Guide

This guide describes the physical wiring between the Raspberry Pi Pico W, the MCP23017 I2C GPIO expanders, motor drivers, and motors. If you are using the MechaPico MCB (custom PCB), most of these connections are pre-routed on the board. This guide helps you understand what is connected where, or wire your own board from scratch.

---

## Table of Contents

1. [Key Concepts](#key-concepts)
2. [MechaPico MCB Overview](#mechapico-mcb-overview)
3. [I2C Bus Connections](#i2c-bus-connections)
4. [MCP23017 U6_1: Stepper Control (0x20)](#mcp23017-u6_1-stepper-control-0x20)
5. [MCP23017 U6_2: DC Motors and LEDs (0x21)](#mcp23017-u6_2-dc-motors-and-leds-0x21)
6. [DC Motor Wiring](#dc-motor-wiring)
7. [Stepper Motor Wiring](#stepper-motor-wiring)
8. [Power Distribution](#power-distribution)
9. [LED Indicators](#led-indicators)
10. [Common Wiring Mistakes](#common-wiring-mistakes)

---

## Key Concepts

### What is a GPIO Pin?

GPIO stands for "General Purpose Input/Output." These are the physical metal pins on the Pico W board that you can connect wires to. Each pin has a number (GP0, GP1, GP2, etc.) and can be configured by the firmware as either:

- **Output:** The pin sends a signal (HIGH = 3.3V, LOW = 0V). Used to control motors, LEDs, etc.
- **Input:** The pin reads a signal from a sensor or button.

### What is I2C?

I2C (pronounced "I-squared-C") is a communication protocol that uses just **two wires** to connect multiple devices. Think of it like a shared telephone line where each device has its own phone number (called an "address"). The Pico W is the "caller" (master) and the MCP23017 chips are "receivers" (slaves).

The two wires are:
- **SDA** (Serial Data): Carries the actual data being sent/received
- **SCL** (Serial Clock): Provides a timing signal so both sides know when to read the data

### What is a Pull-up Resistor?

I2C wires need to be "pulled up" to the supply voltage (3.3V) through resistors. This is because I2C devices communicate by pulling the wire LOW (to 0V) but rely on the pull-up resistor to bring it back HIGH when nobody is talking. Without pull-up resistors, the I2C bus will not work at all.

The Pico W has internal pull-up resistors, which work for short wires (< 10cm). For longer wires, add external 4.7k ohm pull-up resistors from SDA to 3.3V and from SCL to 3.3V.

### What Does "Active LOW" Mean?

Some pins activate their function when set to LOW (0V) instead of HIGH (3.3V). The stepper motor enable pin (`STPR_ALL_EN`) is active LOW, meaning:

- **Pin LOW (0V):** Motors are **enabled** (they can move)
- **Pin HIGH (3.3V):** Motors are **disabled** (they are locked in place or free-spinning)

This is a common convention in electronics. If your motors are not moving, check that the enable pin is LOW.

### How to Read a Pin Table

Pin tables in this guide show the mapping from a chip's pin to its function. Here is how to read them:

| Column | What It Means |
|--------|---------------|
| MCP Pin / GPIO | The physical pin name on the chip or Pico |
| Bit | The bit position in the 8-bit register (0-7) |
| Signal | The signal name used in the firmware code |
| Function | What the pin does in plain English |

---

## MechaPico MCB Overview

The MechaPico MCB routes all connections through two MCP23017 I2C GPIO expanders:

```
                        ┌─────────────────────────────┐
                        │     Raspberry Pi Pico W     │
                        │                             │
     ┌──────────────────┤  GP4 (SDA) ──── I2C0 SDA    │
     │                  │  GP5 (SCL) ──── I2C0 SCL    │
     │                  │  GP22     ──── INT (opt)    │
     │                  │  LED_BUILTIN ─ Status LED   │
     │                  └─────────────────────────────┘
     │                                │
     │            ┌───────────────────┘
     │            │
     ▼            ▼
┌─────────┐  ┌─────────┐
│  U6_1   │  │  U6_2   │
│  0x20   │  │  0x21   │
│ Stepper │  │ DC/LED  │
│ Control │  │ Control │
└────┬────┘  └────┬────┘
     │            │
     ▼            ▼
  M1-M5        DC 3-4
  Steppers     LEDs
               5V I/O
```

> [!IMPORTANT]
> Both MCP23017 chips share the same I2C0 bus. Their unique addresses (0x20 and 0x21) are set by hardware on the PCB via the A0 address pin. Do not change the I2C address jumpers.

---

## I2C Bus Connections

| Signal | Pico GPIO | MCP23017 Pin | Notes |
|--------|:---------:|:------------:|-------|
| SDA | GP4 | SDA (pin 13) | Shared between both chips |
| SCL | GP5 | SCL (pin 12) | Shared between both chips |
| INT | GP22 | INTA/INTB | Optional, currently unused in firmware |

**I2C Configuration:**
- Bus: `i2c0`
- Speed: 400 kHz (Fast mode)
- Pull-ups: Internal pull-ups used. External 4.7k ohm pull-ups recommended for reliability.

> [!TIP]
> If you experience I2C communication errors (motors not responding, intermittent MCP23017 failures), add external 4.7k ohm pull-up resistors from SDA and SCL to 3.3V. Long wires (> 10cm) almost always need external pull-ups.

---

## MCP23017 U6_1: Stepper Control (0x20)

This chip controls all stepper motor signals. The key design decision: all 4 primary motors (M1-M4) are on **Port B**, which allows the firmware to update all of them with a single I2C write instead of four separate writes.

### Port B: Motor Step/Dir Signals

| MCP Pin | Bit | Signal | Motor | Function |
|:-------:|:---:|--------|:-----:|----------|
| GPB0 | 0 | `STPR_M1_DIR` | M1 (Front-Left) | Direction |
| GPB1 | 1 | `STPR_M1_STEP` | M1 (Front-Left) | Step pulse |
| GPB2 | 2 | `STPR_M2_DIR` | M2 (Front-Right) | Direction |
| GPB3 | 3 | `STPR_M2_STEP` | M2 (Front-Right) | Step pulse |
| GPB4 | 4 | `STPR_M3_DIR` | M3 (Back-Left) | Direction |
| GPB5 | 5 | `STPR_M3_STEP` | M3 (Back-Left) | Step pulse |
| GPB6 | 6 | `STPR_M4_DIR` | M4 (Back-Right) | Direction |
| GPB7 | 7 | `STPR_M4_STEP` | M4 (Back-Right) | Step pulse |

### Port A: Motor 5 + Shared Controls

| MCP Pin | Bit | Signal | Function | Notes |
|:-------:|:---:|--------|----------|-------|
| GPA0 | 0 | `LED_BAR_1` | LED bar segment 1 | Status indicator |
| GPA1 | 1 | `STPR_M5_STEP` | Motor 5 step pulse | Spare motor |
| GPA2 | 2 | `STPR_M5_DIR` | Motor 5 direction | Spare motor |
| GPA3 | 3 | `STPR_ALL_EN` | Enable all steppers | **Active LOW**: pull LOW to enable |
| GPA4 | 4 | `STPR_ALL_MS1` | Microstepping bit 1 | See [Microstepping Reference](CONFIGURATION_REFERENCE.md#microstepping-reference) |
| GPA5 | 5 | `STPR_ALL_MS2` | Microstepping bit 2 | See [Microstepping Reference](CONFIGURATION_REFERENCE.md#microstepping-reference) |
| GPA6 | 6 | `STPR_ALL_SPRD` | SpreadCycle mode | TMC2209 only: HIGH = high-torque mode |
| GPA7 | 7 | `STPR_ALL_PDN` | Power Down / UART | HIGH = standalone step/dir mode |

> [!WARNING]
> `STPR_ALL_EN` (GPA3) is **active LOW**. Set it LOW to enable motors. If motors are not moving after initialization, check that this pin is being driven LOW.
>
> `STPR_ALL_PDN` (GPA7) must be HIGH for standalone step/dir operation. If LOW, TMC2209 enters UART mode and will not respond to step pulses.

---

## MCP23017 U6_2: DC Motors and LEDs (0x21)

### Port A: DC Motor Control and LED Bar

| MCP Pin | Bit | Signal | Function |
|:-------:|:---:|--------|----------|
| GPA0 | 0 | `IN_MOT_4N` | Motor 4 negative |
| GPA1 | 1 | `IN_MOT_4P` | Motor 4 positive |
| GPA2 | 2 | `IN_MOT_3N` | Motor 3 negative |
| GPA3 | 3 | `IN_MOT_3P` | Motor 3 positive |
| GPA4 | 4 | `LED_BAR_2` | LED bar segment 2 |
| GPA5 | 5 | `LED_BAR_3` | LED bar segment 3 |
| GPA6 | 6 | `LED_BAR_4` | LED bar segment 4 |
| GPA7 | 7 | `LED_BAR_5` | LED bar segment 5 |

### Port B: General Purpose 5V I/O

| MCP Pin | Bit | Signal | Function |
|:-------:|:---:|--------|----------|
| GPB0 | 0 | `IO_5V_0` | 5V general purpose I/O |
| GPB1 | 1 | `IO_5V_1` | 5V general purpose I/O |
| GPB2 | 2 | `IO_5V_2` | 5V general purpose I/O |
| GPB3 | 3 | `IO_5V_3` | 5V general purpose I/O |
| GPB4 | 4 | `IO_5V_4` | 5V general purpose I/O |
| GPB5 | 5 | `IO_5V_5` | 5V general purpose I/O |
| GPB6 | 6 | `IO_5V_6` | 5V general purpose I/O |
| GPB7 | 7 | `IO_5V_7` | 5V general purpose I/O |

> [!NOTE]
> The 5V I/O pins on Port B are spare general-purpose outputs at 5V logic level. They can be used for additional LEDs, relays, or sensor readback. The MCP23017's open-drain outputs are 5V tolerant.

---

## DC Motor Wiring

If using DC motors with direct Pico GPIO (not through the MCP23017), wire according to your driver:

### DRV8871 (Single H-bridge, 2 pins per motor)

```
  Pico GPIO ──→ IN1 ──┐
                       ├──→ DRV8871 ──→ Motor
  Pico GPIO ──→ IN2 ──┘

  PWM on IN1, IN2 LOW  = Forward
  IN1 LOW, PWM on IN2  = Reverse
  Both LOW              = Coast (motor free-spins)
```

| Motor | IN1 | IN2 |
|-------|:---:|:---:|
| Front-Left | GP2 | GP3 |
| Front-Right | GP4 | GP5 |
| Back-Left | GP6 | GP7 |
| Back-Right | GP8 | GP9 |

### DRV8833 (Dual H-bridge, 2 motors per chip)

```
  Chip 1:                   Chip 2:
  ┌────────────┐            ┌────────────┐
  │ AIN1 ← GP2 │ Motor FL   │ AIN1 ← GP6 │ Motor BL
  │ AIN2 ← GP3 │            │ AIN2 ← GP7 │
  │ BIN1 ← GP4 │ Motor FR   │ BIN1 ← GP8 │ Motor BR
  │ BIN2 ← GP5 │            │ BIN2 ← GP9 │
  └────────────┘            └────────────┘
```

### L298N (Dual H-bridge with enable pin)

```
  ┌──────────────────┐
  │ ENA ← GP2 (PWM)  │ Motor FL speed
  │ IN1 ← GP3        │ Motor FL direction
  │ IN2 ← GP4        │ Motor FL direction
  │ ENB ← GP5 (PWM)  │ Motor FR speed
  │ IN3 ← GP6        │ Motor FR direction
  │ IN4 ← GP7        │ Motor FR direction
  └──────────────────┘
  (Second L298N module for back motors: GP8-GP13)
```

> [!TIP]
> PWM frequency is set to 20kHz in firmware to avoid audible motor whine. The 8-bit resolution gives 256 speed steps (0 to 255).

---

## Stepper Motor Wiring

### Via MCP23017 (MechaPico MCB)

Stepper drivers connect to the MCP23017 U6_1 (address 0x20), **not** directly to Pico GPIO:

```
  MCP23017 U6_1         TMC2209 / A4988 / DRV8825           Motor
  ┌───────────┐         ┌─────────────────────┐         ┌────────┐
  │ GPBx STEP ├────────→│ STEP                │         │        │
  │ GPBx DIR  ├────────→│ DIR                 │────────→│ Coil A │
  │ GPA3 EN   ├────────→│ EN (active LOW)     │────────→│ Coil B │
  │ GPA4 MS1  ├────────→│ MS1                 │         │        │
  │ GPA5 MS2  ├────────→│ MS2                 │         └────────┘
  │ GPA7 PDN  ├────────→│ PDN_UART            │
  └───────────┘         └─────────────────────┘
```

### Motor to MCP23017 Pin Mapping

| Motor | Position | STEP Pin | DIR Pin | Port |
|:-----:|----------|:--------:|:-------:|:----:|
| M1 | Front-Left | GPB1 | GPB0 | B |
| M2 | Front-Right | GPB3 | GPB2 | B |
| M3 | Back-Left | GPB5 | GPB4 | B |
| M4 | Back-Right | GPB7 | GPB6 | B |
| M5 | Spare | GPA1 | GPA2 | A |

### Shared Stepper Control Signals

All stepper drivers share these signals (all 5 motors are affected simultaneously):

| Signal | MCP Pin | Default State | Purpose |
|--------|:-------:|:-------------:|---------|
| Enable | GPA3 | LOW (enabled) | Active LOW: motors energized when LOW |
| MS1 | GPA4 | LOW | Microstepping bit 1 |
| MS2 | GPA5 | LOW | Microstepping bit 2 |
| SpreadCycle | GPA6 | HIGH | TMC2209: HIGH enables high-torque mode |
| PDN/UART | GPA7 | HIGH | HIGH = standalone step/dir; LOW = UART mode |

---

## Power Distribution

```
                    ┌───────────────────────┐
  Battery ─────────┤ Input (6V-36V)        │
  (or PSU)         │                       │
                   │  5V regulator ────────┼──→ MCP23017 VDD
                   │                       │──→ Stepper driver VCC (logic)
                   │  3.3V (from Pico) ────┼──→ Pico 3V3OUT
                   │                       │
                   │  VMOT ────────────────┼──→ Stepper driver VMOT (motor coils)
                   └───────────────────────┘
```

| Rail | Voltage | Supplies | Notes |
|------|:-------:|----------|-------|
| VMOT | 6-36V | Stepper motor coils | Must match your motor's rated voltage |
| VCC/5V | 5V | MCP23017 VDD, driver logic | Regulated from input voltage |
| 3V3 | 3.3V | Pico W operating voltage | From Pico's internal regulator |
| GND | 0V | All common ground | **All grounds must be connected** |

> [!CAUTION]
> **Common ground is critical.** The Pico W, both MCP23017 chips, all motor drivers, and the power supply MUST share a common ground. Without it, I2C communication will be unreliable and motors may behave erratically. This is the single most common wiring mistake.

---

## LED Indicators

| LED | Location | Meaning |
|-----|----------|---------|
| Built-in LED (Pico) | On Pico W board | ON = BLE connected, OFF = disconnected/advertising |
| LED_BAR_1 | MCP23017 0x20, GPA0 | Status indicator (user-definable in firmware) |
| LED_BAR_2 | MCP23017 0x21, GPA4 | Status indicator (user-definable) |
| LED_BAR_3 | MCP23017 0x21, GPA5 | Status indicator (user-definable) |
| LED_BAR_4 | MCP23017 0x21, GPA6 | Status indicator (user-definable) |
| LED_BAR_5 | MCP23017 0x21, GPA7 | Status indicator (user-definable) |

---

## Common Wiring Mistakes

These are the top 5 mistakes students make when wiring their robots:

| # | Mistake | Symptom | Fix |
|:-:|---------|---------|-----|
| 1 | **Missing common ground** | I2C fails intermittently, motors behave erratically | Connect all GND pins together: Pico, MCP23017, drivers, power supply |
| 2 | **Swapped SDA and SCL** | I2C init fails completely (`[MCP23017] Init failed`) | Double-check: SDA = GP4, SCL = GP5. They are NOT interchangeable. |
| 3 | **Motor direction wires swapped** | Robot turns instead of going straight | Swap the two motor wires at the driver output, OR flip direction in `project_config.h` |
| 4 | **No pull-up resistors on long I2C wires** | Works sometimes, fails randomly | Add 4.7k ohm resistors from SDA to 3.3V and SCL to 3.3V |
| 5 | **Wrong microstepping for driver type** | Motor runs at wrong speed or vibrates | TMC2209 MS1=0, MS2=0 = 8 microsteps. A4988 MS1=0, MS2=0 = full step. See [Microstepping Reference](CONFIGURATION_REFERENCE.md#microstepping-reference). |
