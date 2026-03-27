/**
 * @file main.cpp
 * @brief Main Entry Point for Raspberry Pi Pico W Robot Controller
 *
 * ============================================================================
 * DUAL-CORE ARCHITECTURE
 * ============================================================================
 *
 *  ┌─── Core 0 ───────────────────────┐  ┌─── Core 1 ─────────────┐
 *  │  BTstack BLE (advertising,       │  │  simple_stepper loop    │
 *  │    GATT server, pairing)         │  │  (runs every 500µs)     │
 *  │  command_parser (JSON → struct)  │  │                         │
 *  │  profile_mecanum (kinematics)    │  │  Reads g_targetSpeeds[] │
 *  │  safety_check_timeout()          │  │  via mutex_try_enter()  │
 *  │                                  │  │                         │
 *  │  Writes g_targetSpeeds[] ────────┼──►  Generates step pulses  │
 *  │  via mutex_enter_blocking()      │  │  via MCP23017 I2C       │
 *  └──────────────────────────────────┘  └─────────────────────────┘
 *
 * WHY TWO CORES?
 *   Stepper motors need precise pulse timing (microsecond-level). BLE stack
 *   processing (BTstack) can block for 5-10ms during connection events. If
 *   both ran on one core, BLE pauses would cause missed steps and motor
 * stutter. Dedicating Core 1 to pulse generation guarantees deterministic
 * timing.
 *
 * WHY MUTEX INSTEAD OF FIFO/QUEUE?
 *   We only need the LATEST speed command, not a queue of all past commands.
 *   A mutex-protected shared variable is simpler and lower-latency than a FIFO.
 *   Core 0 writes new speeds; Core 1 reads them. Stale values are fine since
 *   they'll be overwritten on the next joystick update (~50Hz).
 *
 * SETUP1() / LOOP1() CONVENTION:
 *   The Arduino-Pico core (earlephilhower) automatically runs setup1() and
 *   loop1() on Core 1. No manual multicore_launch_core1() needed. These
 *   functions are simply defined here and the framework handles the rest.
 *
 * DATA FLOW:
 *   Flutter App -> BLE JSON -> command_parser -> profile_mecanum
 *     -> mecanum_kinematics -> g_targetSpeeds[] (via mutex) -> Core 1
 *     -> simple_stepper -> MCP23017 I2C batch writes -> stepper motors
 */

#include "pico/mutex.h"
#include <Arduino.h>
#include <Wire.h>

// Project configuration
#include "project_config.h"

// Core modules
#include "core/ble_controller.h"
#include "core/command_parser.h"
#include "core/motor_manager.h"
#include "core/safety.h"
#include "core/simple_stepper.h"
#include "drivers/mcp23017.h"

// Profiles
#include "profiles/profile_mecanum.h"
#if defined(ENABLE_MOTOR_5) || defined(ENABLE_DC_MOTOR_3) || defined(ENABLE_DC_MOTOR_4)
#include "profiles/profile_aux_motors.h"
#endif

// =============================================================================
// INTER-CORE COMMUNICATION (shared between Core 0 and Core 1)
// =============================================================================

// Mutex for thread-safe access to shared speed data
mutex_t g_speedMutex;

// Shared speed commands (written by Core 0, read by Core 1)
#ifdef ENABLE_MOTOR_5
volatile float g_targetSpeeds[5] = {0, 0, 0, 0, 0};
#else
volatile float g_targetSpeeds[4] = {0, 0, 0, 0};
#endif
volatile bool g_speedsUpdated = false;
volatile bool g_emergencyStop = false;

// =============================================================================
// GLOBAL STATE (Core 0 only)
// =============================================================================

static bool s_bleConnected = false;
static unsigned long s_lastUpdateTime = 0;

// =============================================================================
// CORE 1: STEPPER PULSE GENERATION
// =============================================================================

/**
 * @brief Core 1 setup: runs automatically on the second CPU core.
 *
 * @details The Arduino-Pico framework calls setup1() on Core 1 at boot,
 * in parallel with setup() on Core 0. The 2-second delay ensures that
 * Core 0 has finished initializing the I2C bus and MCP23017 chip before
 * Core 1 tries to use them for step pulse generation.
 *
 * If you remove the delay, Core 1 may attempt I2C writes to a chip that
 * hasn't been configured yet, resulting in garbage pin states and motor
 * jitter on first boot.
 *
 * @see simple_stepper_init() which sets up the accumulator-based timing
 */
void setup1() {
  // Wait for Core 0 to initialize I2C and MCP23017 first
  delay(2000);

  // Initialize the stepper system on Core 1
  simple_stepper_init();
}

/**
 * @brief Core 1 main loop: dedicated to stepper pulse generation.
 *
 * @details This function runs thousands of times per second on Core 1.
 * Each iteration does two things:
 *   1. Checks if Core 0 posted new speed commands (non-blocking mutex).
 *   2. Calls simple_stepper_update() which generates step pulses.
 *
 * The mutex_try_enter() is NON-BLOCKING: if Core 0 currently holds the
 * mutex (writing new speeds), Core 1 skips the check and immediately
 * generates pulses using the previous speeds. This ensures step timing
 * is never delayed by Core 0 activity.
 *
 * Priority order: emergency stop > speed update > pulse generation.
 *
 * @warning Do NOT add Serial.printf() here. Serial output takes ~8ms
 *          on Pico W and would destroy step timing precision.
 *
 * @see simple_stepper_update() for the pulse generation logic
 * @see profile_mecanum_apply() on Core 0 which writes g_targetSpeeds[]
 */
void loop1() {
  // Check for new speed commands (non-blocking)
  if (mutex_try_enter(&g_speedMutex, nullptr)) {
    if (g_emergencyStop) {
      // Safety triggered on Core 0: stop all motors immediately
      simple_stepper_stop_all();
      g_emergencyStop = false;
    } else if (g_speedsUpdated) {
      // New joystick speeds from Core 0: apply to the stepper engine
      for (int i = 0; i < 4; i++) {
        simple_stepper_set_speed(i, g_targetSpeeds[i]);
      }
#ifdef ENABLE_MOTOR_5
      simple_stepper_set_speed(4, g_targetSpeeds[4]);
#endif
      g_speedsUpdated = false;
    }
    mutex_exit(&g_speedMutex);
  }

  // Generate step pulses (runs at 500us intervals internally)
  simple_stepper_update();
}

// =============================================================================
// CORE 0: BLE CALLBACKS
// =============================================================================

/**
 * @brief BLE command callback: called every time the app sends data.
 *
 * @details This is the entry point for ALL incoming commands (joystick,
 * heartbeat, etc.). It runs on Core 0 from BTstack's event loop.
 *
 * The processing order matters:
 *   1. Feed the safety watchdog FIRST (even before parsing). This ensures
 *      that even malformed commands reset the timeout.
 *   2. Parse the JSON into a command struct.
 *   3. If heartbeat: stop here. Heartbeats exist only to feed the watchdog.
 *   4. If control: route to the appropriate motion profile.
 *
 * @param jsonData  Raw JSON string from BLE (null-terminated).
 * @param length    String length in bytes.
 *
 * @see ble_init() where this callback is registered
 * @see profile_mecanum_apply() which processes mecanum commands
 */
void onBleCommand(const char *jsonData, uint16_t length) {
  // Feed safety watchdog FIRST - ensures any received data resets the
  // dead-man timer, even if the JSON is malformed
  safety_feed();

  // Parse JSON into a command struct
  control_command_t cmd;
  if (!command_parse(jsonData, &cmd)) {
    return; // Malformed JSON - error already logged by command_parser
  }

  // Heartbeats only exist to feed the watchdog (done above). No motor
  // action needed.
  if (command_is_heartbeat()) {
    return;
  }

  // Route to the main drive motion profile
  if (strcmp(cmd.vehicle, "mecanum") == 0) {
#ifdef MOTION_PROFILE_MECANUM
    profile_mecanum_apply(&cmd);
#endif
  }

  // Route to auxiliary motors (Motor 5, DC Motors 3-4)
#if defined(ENABLE_MOTOR_5) || defined(ENABLE_DC_MOTOR_3) || defined(ENABLE_DC_MOTOR_4)
  profile_aux_motors_apply(&cmd);
#endif
}

/**
 * @brief BLE connection state callback: called on connect and disconnect.
 *
 * @details On disconnect, this triggers a two-pronged emergency stop:
 *   1. Sets g_emergencyStop flag (via mutex) so Core 1 stops pulses.
 *   2. Calls motors_stop_all() on Core 0 for DC motors.
 *
 * The blocking mutex (mutex_enter_blocking) is used here because
 * disconnect is rare and safety-critical. We MUST guarantee that
 * Core 1 sees the stop flag.
 *
 * @param connected  true = phone just connected, false = disconnected.
 *
 * @see ble_init() where this callback is registered
 */
void onBleConnectionChange(bool connected) {
  s_bleConnected = connected;

  if (connected) {
    Serial.println(">>> Client connected!");
  } else {
    Serial.println(">>> Client disconnected - stopping motors");

    // Signal Core 1 to stop all motors (blocking mutex because safety
    // is more important than latency here)
    mutex_enter_blocking(&g_speedMutex);
    g_emergencyStop = true;
    mutex_exit(&g_speedMutex);

    // Also stop DC motors directly on Core 0 (they don't use Core 1)
    motors_stop_all();

#if defined(ENABLE_MOTOR_5) || defined(ENABLE_DC_MOTOR_3) || defined(ENABLE_DC_MOTOR_4)
    // Stop auxiliary motors (DC motors on MCP23017 U6_2)
    profile_aux_motors_stop();
#endif
  }
}

// =============================================================================
// CORE 0: ARDUINO SETUP
// =============================================================================

void setup() {
  // Initialize mutex FIRST (before Core 1 starts using it)
  mutex_init(&g_speedMutex);

  // Initialize serial for debugging
  Serial.begin(115200);

  // Wait 10 seconds for serial monitor reconnection after flash
  delay(10000);
  while (!Serial && millis() < 13000) {
    // Wait for serial (additional 3 seconds if not ready)
  }

  Serial.println();
  Serial.println("===========================================");
  Serial.println("   RC Robot Controller - Pico W Edition");
  Serial.println("        MULTICORE ARCHITECTURE v2.0");
  Serial.println("===========================================");
  Serial.printf("Device Name: %s\n", DEVICE_NAME);
  Serial.printf("PIN Code: %06d\n", BLE_PASSKEY);
  Serial.println("Core 0: BLE + Commands");
  Serial.println("Core 1: Stepper Pulses");
  Serial.println();

  // Initialize LED for status
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Initialize safety watchdog
  safety_init();

  // =========================================================================
  // MOTOR INITIALIZATION (Core 0 - I2C setup)
  // =========================================================================
  Serial.println("[Core0] Initializing motors...");
  bool motorsOk = motors_init();
  if (!motorsOk) {
    Serial.println("ERROR: Motor initialization failed!");
    Serial.println("       MCP23017 not responding - check I2C wiring");
  } else {
    Serial.println("[Core0] Motors initialized OK");
  }

  // STEPPER INITIALIZATION (only if motors initialized)
#if defined(STEPPER_DRIVER_TMC2209) || defined(STEPPER_DRIVER_A4988) ||        \
    defined(STEPPER_DRIVER_DRV8825)
  if (motorsOk) {
    // NOTE: Microstepping already configured by motor_manager based on
    // STEPPER_MICROSTEPPING Just set additional TMC2209-specific pins here
    mcpStepper.setBitA(STPR_ALL_SPRD_BIT, false); // StealthChop (quiet mode)
    mcpStepper.setBitA(STPR_ALL_PDN_BIT,
                       true); // PDN=1 for standalone STEP/DIR mode
    // EN already set by motor_manager

    Serial.printf("[Core0] Steppers ready (%d microsteps)\n",
                  STEPPER_MICROSTEPPING);
  }
#endif

  // =========================================================================
  // BLE INITIALIZATION (Core 0 - after motors to avoid I2C conflicts)
  // =========================================================================
  ble_init(onBleCommand, onBleConnectionChange);

  Serial.println();
  Serial.println("[Core0] Setup complete - Core 1 handles stepper pulses");
  Serial.println();

  s_lastUpdateTime = millis();
}

// =============================================================================
// CORE 0: ARDUINO LOOP (BLE only - no stepper updates!)
// =============================================================================

void loop() {
  // Process BLE events (this is Core 0's main job)
  ble_update();

  // Motor update at 50Hz (safety check only)
  unsigned long now = millis();
  if (now - s_lastUpdateTime >= MOTOR_UPDATE_INTERVAL_MS) {
    s_lastUpdateTime = now;

    // Check safety timeout
    if (s_bleConnected && safety_check_timeout()) {
      // Signal Core 1 to stop
      mutex_enter_blocking(&g_speedMutex);
      g_emergencyStop = true;
      mutex_exit(&g_speedMutex);
    }
  }

  // Status LED: blink when disconnected, solid when connected
  if (!s_bleConnected) {
    static unsigned long lastBlink = 0;
    if (now - lastBlink >= 500) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      lastBlink = now;
    }
  }
}
