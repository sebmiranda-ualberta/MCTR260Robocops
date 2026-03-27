# Troubleshooting Guide

Something is not working? This guide covers the most common problems and their solutions. Start from the top and work your way down.

---

## Table of Contents

1. [Before You Start: How to Use the Serial Monitor](#before-you-start-how-to-use-the-serial-monitor)
2. [Build and Upload Issues](#build-and-upload-issues)
3. [BLE Connectivity Issues](#ble-connectivity-issues)
4. [Motor Issues (General)](#motor-issues-general)
5. [Stepper-Specific Issues](#stepper-specific-issues)
6. [MCP23017 / I2C Issues](#mcp23017--i2c-issues)
7. [Safety System Issues](#safety-system-issues)
8. [LED Status Reference](#led-status-reference)
9. [Still Stuck?](#still-stuck)

---

## Before You Start: How to Use the Serial Monitor

The serial monitor is your **primary debugging tool**. It shows text messages that the firmware prints as it runs. Before trying to fix anything, open the serial monitor and read what the firmware is telling you.

### How to Open the Serial Monitor

1. Connect the Pico W to your computer via USB
2. In VS Code, open the terminal: `View → Terminal` (or press `` Ctrl+` ``)
3. Run:
   ```
   pio run -t monitor
   ```
4. If the Pico was already running, press the reset button on the board to restart and see the full boot sequence

### How to Read Serial Output

Each subsystem in the firmware uses a **tag prefix** in square brackets so you can quickly identify which part of the code is talking:

| Tag | Source File | What It Shows |
|-----|------------|---------------|
| `[BLE]` | `ble_controller.cpp` | Connection events, MAC address, advertising state |
| `[CMD]` | `command_parser.cpp` | Received JSON commands and parsed values |
| `[MotorDC]` | `motor_dc.cpp` | DC motor initialization and PWM values |
| `[MotorStepper]` | `motor_stepper.cpp` | Stepper initialization and speed changes |
| `[MotorManager]` | `motor_manager.cpp` | Motor registry and initialization sequence |
| `[Safety]` | `safety.cpp` | Watchdog feed events and timeout triggers |
| `[SimpleStepper]` | `simple_stepper.cpp` | Core 1 step pulse generation (minimal output to avoid performance impact) |
| `[Mecanum]` | `profile_mecanum.cpp` | Kinematics output (wheel speeds) |
| `[MCP23017]` | `mcp23017.cpp` | I2C initialization and register write results |

### Expected Boot Sequence

When the firmware starts correctly, you should see output like this. Each line is annotated with what it means:

```
[MCP23017] Initializing I2C: SDA=4, SCL=5, freq=400000    ← I2C bus started successfully
[MCP23017] Stepper MCP (0x20) initialized                  ← Stepper GPIO expander responding
[MCP23017] DC Motor MCP (0x21) initialized                 ← DC/LED GPIO expander responding
[MotorManager] Registering 4 stepper motors                ← Motor manager found 4 motors to register
[MotorStepper] Motor 0 initialized (TMC2209)               ← Each motor init confirms driver type
[MotorStepper] Motor 1 initialized (TMC2209)
[MotorStepper] Motor 2 initialized (TMC2209)
[MotorStepper] Motor 3 initialized (TMC2209)
[BLE] Derived address: C0:AB:12:34:56:78 (hash: 0x1234)   ← Bluetooth MAC derived from name+PIN
[BLE] Device name: TimberBot_RC                            ← This is the name the app will show
[BLE] Advertising started - waiting for connection...      ← Pico is broadcasting, ready for app
[Safety] Watchdog initialized (timeout: 2000ms)            ← Safety system active
```

**If any line is missing, the corresponding subsystem failed to initialize.** Read the specific troubleshooting section below for that subsystem.

> [!WARNING]
> If you see **no output at all**, or only garbage characters, check that the serial monitor baud rate is **115200**. PlatformIO sets this correctly by default, but if you are using a different terminal program, you may need to set it manually.

---

## Build and Upload Issues

| Issue | Likely Cause | Solution |
|-------|-------------|----------|
| `compile error: multiple motor drivers` | Two driver `#define` lines are uncommented | Open `project_config.h` and ensure only **one** motor driver `#define` is active. Comment out all others with `//`. |
| `undefined reference to Arduino.h` | Wrong PlatformIO platform | Ensure `platformio.ini` uses `platform = raspberrypi` with `framework = arduino` |
| Upload fails: "port busy" | Serial monitor is using the USB port | Close the serial monitor first, then retry upload. They cannot share the port. |
| Upload fails: "no device found" | Pico not in bootloader mode | Hold the **BOOTSEL** button on the Pico while plugging in USB, then release. Retry upload. |
| `pico/mutex.h not found` | RP2040 Arduino core not installed | Run `pio pkg install` to install the earlephilhower RP2040 core |
| Build takes 5+ minutes | First build downloads everything | This is normal for the first build. Subsequent builds are 10-30 seconds. |

---

## BLE Connectivity Issues

| Issue | Likely Cause | Solution |
|-------|-------------|----------|
| Robot not appearing in scan list | BLE not advertising | Check serial output for `[BLE] Advertising started`. If missing, BLE initialization failed. Check the serial log for errors above that line. |
| Robot appears but won't connect | Stale pairing on phone | Delete the old Bluetooth pairing from your phone's Settings → Bluetooth → (device) → Forget, then re-scan. |
| Robot appears low in scan list | Name missing priority keyword | Include `robot`, `rc`, `pico`, `mecanum`, `tank`, `meca`, or `controller` in `DEVICE_NAME` |
| PIN dialog not appearing | Security not configured | Verify `BLE_PASSKEY` is set in `project_config.h`. Check serial for security-related messages. |
| Wrong PIN rejected | PIN mismatch | Ensure the 6-digit `BLE_PASSKEY` in firmware matches exactly what you enter in the app |
| Connected but no motor response | Commands not reaching firmware | Check serial for `[CMD]` messages after pressing joystick. If none: UUID mismatch between firmware and app. |
| Connects then disconnects immediately | UUID mismatch | Compare UUIDs in `ble_config.h` with the Flutter app's `ble_constants.dart`. They must be identical. |
| "New device" after firmware update | MAC address changed | You changed `DEVICE_NAME` or `BLE_PASSKEY`. Delete old pairing from phone, then re-pair. This is expected behavior. |
| Intermittent disconnects | Range or interference | Move closer (BLE range: 5-10m indoors). Reduce WiFi interference. Check that `SAFETY_TIMEOUT_MS` is not set too low. |

> [!TIP]
> **Quick BLE diagnostic:** Open serial monitor at 115200 baud. You should see:
> ```
> [BLE] Derived address: C0:...
> [BLE] Device name: YourRobotName
> [BLE] Advertising started
> ```
> If any of these are missing, BLE initialization failed. Check for error messages above these lines in the serial output.

---

## Motor Issues (General)

| Issue | Likely Cause | Solution |
|-------|-------------|----------|
| No motors moving at all | Wrong motor type enabled | Check `project_config.h`. Only **one** driver `#define` should be uncommented. |
| DC motors not spinning | Wrong GPIO pins | Verify pin numbers in `project_config.h` match your physical wiring |
| DC motors spin wrong direction | Direction swap needed | Swap `pinA`/`pinB` in config, OR physically swap the two motor wires at the driver output |
| DC motors make whining noise | PWM frequency too low | Firmware uses 20kHz by default. If you modified it, restore `analogWriteFreq(20000)` in `motor_dc.cpp` |
| Motor speed does not match joystick | Geometry values wrong | Re-measure `WHEEL_RADIUS_MM`, `WHEELBASE_HALF_MM`, `TRACK_WIDTH_HALF_MM` with calipers |
| Robot drifts instead of going straight | Geometry asymmetry | Verify all 4 wheels are identical size and track width is measured accurately |
| Only some motors work | Partial wiring issue | Check each motor's GPIO connections individually. Serial output shows which motors initialized successfully. |

---

## Stepper-Specific Issues

| Issue | Likely Cause | Solution |
|-------|-------------|----------|
| Steppers not moving | Enable pin not LOW | Check serial for `stepperEnableAll()`. Verify GPA3 (`STPR_ALL_EN`) is driven LOW by the MCP23017. |
| Steppers vibrate but do not rotate | Wrong microstepping config | Verify `STEPPER_MICROSTEPPING` matches the MS1/MS2 pin states. TMC2209 and A4988 have **different** truth tables! |
| Audible clicking/rattling | Missed steps | Reduce `STEPPER_MAX_SPEED` or increase `STEPPER_ACCELERATION` for a gentler ramp |
| Jittery motion | Core 1 timing issue | Check I2C bus for contention. Ensure no Core 0 code is accessing the I2C bus during motor operation. |
| Very slow stepper response | Pulse interval too high | Default is 500µs. Reducing to 250µs doubles max speed but increases CPU load. |
| Steppers lose position over time | Acceleration too aggressive | Lower `STEPPER_ACCELERATION` or use higher microstepping for smoother ramping |
| TMC2209 not responding to STEP | PDN pin is LOW (UART mode) | Ensure GPA7 (`STPR_ALL_PDN`) is HIGH. When LOW, TMC2209 enters UART configuration mode and ignores step pulses. |
| One motor works, others do not | Individual MCP23017 wiring | Check the specific motor's STEP/DIR bits. See [Wiring Guide: Port B](WIRING_GUIDE.md#port-b-motor-stepdir-signals). |

> [!IMPORTANT]
> **TMC2209 vs A4988 microstepping:** These drivers have **different** truth tables! MS1=0, MS2=0 gives **8 microsteps** on TMC2209 but **full step** on A4988. If your motor is running at the wrong speed, this mismatch is almost certainly the cause. See [Microstepping Reference](CONFIGURATION_REFERENCE.md#microstepping-reference).

---

## MCP23017 / I2C Issues

| Issue | Likely Cause | Solution |
|-------|-------------|----------|
| `[MCP23017] Init failed at 0x20` | I2C wiring wrong | Check SDA (GP4) and SCL (GP5) wiring. Are they swapped? |
| `[MCP23017] Init failed at 0x21` | Wrong I2C address | Verify the A0 jumper on the PCB matches the 0x21 address configuration |
| Intermittent I2C failures | Missing pull-ups | Add 4.7k ohm pull-up resistors from SDA and SCL to 3.3V. Internal pull-ups are weak. |
| I2C works at startup, fails later | Bus contention | Both cores may be accessing I2C simultaneously. The firmware is designed to prevent this; check for custom code that breaks this rule. |
| All steppers fail simultaneously | Shared signals wrong | Check GPA3 (enable, must be LOW) and GPA7 (PDN, must be HIGH). Both must be correct for any stepper to work. |
| Slow I2C, motors cannot reach full speed | Wrong I2C frequency | Verify `MCP23017_I2C_FREQ` is 400000 (400kHz). At 100kHz, step rate is severely limited. |

> [!TIP]
> **I2C bus scan:** Add this temporary code to `setup()` to check which devices are connected:
> ```cpp
> for (uint8_t addr = 0x08; addr < 0x78; addr++) {
>     uint8_t data;
>     if (i2c_read_blocking(i2c0, addr, &data, 1, false) >= 0) {
>         Serial.printf("I2C device found at 0x%02X\n", addr);
>     }
> }
> ```
> You should see `0x20` and `0x21` for a MechaPico MCB. If neither appears, your I2C wiring is wrong.

---

## Safety System Issues

| Issue | Likely Cause | Solution |
|-------|-------------|----------|
| Motors stop every ~2 seconds | Safety timeout triggering | The app's heartbeat interval must be less than `SAFETY_TIMEOUT_MS` (default 2000ms). If the app is not sending heartbeats fast enough, increase the timeout. |
| Motors do not stop on disconnect | Watchdog disabled | Check serial for `[Safety]` messages. Verify `safety_init()` is called in `setup()`. |
| False emergency stops | BLE latency spikes | Increase `SAFETY_TIMEOUT_MS` to 3000-5000ms. Trade-off: higher values mean the robot takes longer to stop after a real disconnect. |
| Motors keep running after app closes | Disconnect not detected | Check the `onBleConnectionChange` callback in `main.cpp`. It should call `motors_stop_all()` on disconnect. |

> [!NOTE]
> The safety system has **two independent layers:**
> 1. **Heartbeat watchdog:** Times out if no command arrives within `SAFETY_TIMEOUT_MS`
> 2. **BLE disconnect callback:** Immediately stops motors when the Bluetooth connection drops
>
> Both trigger motor stop independently. If one fails, the other acts as a backup. If motors keep running after a disconnect, both layers have failed, which usually means a firmware bug rather than a wiring issue.

---

## LED Status Reference

| LED | State | Meaning |
|-----|-------|---------|
| Pico built-in LED | **ON** (solid) | BLE device connected |
| Pico built-in LED | **OFF** | No BLE connection (advertising and waiting) |
| LED_BAR_1 through LED_BAR_5 | User-defined | Available for custom status indicators in firmware |

---

## Custom Code Issues

If you are adding your own profiles, motors, or sensors (see [EXTENDING_THE_FIRMWARE.md](EXTENDING_THE_FIRMWARE.md)), these are the most common problems:

| Issue | Likely Cause | Solution |
|-------|-------------|----------|
| Code compiles but nothing happens | Profile `#define` missing | Ensure your profile guard (`#define MOTION_PROFILE_YOURNAME`) is uncommented in `project_config.h` AND your `profile_init()` call is added inside the corresponding `#ifdef` block in `main.cpp`. |
| Motor 5 moves erratically | I2C bus contention | Motor 5 (Port A) shares the I2C bus with Core 1's stepper writes (Port B). Wrap MCP23017 0x20 access with `mutex_try_enter(&g_speedMutex)`. See [EXTENDING_THE_FIRMWARE.md: Adding I2C Devices](EXTENDING_THE_FIRMWARE.md#adding-i2c-devices). |
| All drive motors stutter after adding code | `delay()` blocking Core 0 | Any `delay()` on Core 0 stalls the BLE event loop. Use timing variables with `micros()` instead. |
| Serial output causes motor jitter | `Serial.printf()` in Core 1 | Never print from `loop1()` or `simple_stepper_update()`. Serial I/O takes ~8ms and destroys step timing. |
| Compile error: undefined reference | Missing `extern` or `#include` | Check that you include the correct headers and declare any external variables with `extern`. |
| Aux channel reads always zero | Wrong channel index | `aux[]` indices are 0-5. Check that the Flutter app is sending data on the channel you expect. Use serial monitor with `[CMD]` tag to verify. |

---

## Still Stuck?

If you have tried the solutions above and your robot still is not working:

1. **Re-read the serial output carefully.** The answer is almost always in the serial log. Look for error messages or missing initialization lines.
2. **Check your wiring.** Use a multimeter to verify continuity on every connection. The most common hardware bug is a loose wire.
3. **Simplify.** Disconnect everything except the Pico W and serial monitor. Does the boot sequence complete? Add one component at a time until it breaks.
4. **Compare with a working setup.** If another student's robot works, compare their `project_config.h` and wiring with yours.
5. **Ask your instructor.** Bring the serial monitor output and a photo of your wiring. These two things will let them diagnose the issue quickly.
