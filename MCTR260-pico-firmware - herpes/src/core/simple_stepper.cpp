/**
 * @file simple_stepper.cpp
 * @brief Core 1 stepper pulse engine: the production step generator
 *
 * This is the lightweight, procedural stepper module that actually runs on
 * Core 1 in production. It replaces the OOP MotorStepper class for runtime
 * use because it supports BATCH I2C writes (all 4 motors in 2 I2C transactions
 * instead of 8).
 *
 * TIMING MODEL (accumulator-based):
 *   Instead of computing step intervals (uss per step), this module accumulates
 *   fractional steps each update:
 *     accumulator += speed x dt
 *     if accumulator >= 1.0: generate a step, subtract 1.0
 *   This naturally handles variable update rates and avoids integer division.
 *
 * ZERO-CROSSING PROTECTION:
 *   When the joystick reverses direction, the speed target flips sign.
 *   Without protection, the motor would briefly cross zero and generate
 *   steps in the wrong direction (audible as a "click"). The zero-crossing
 *   guard clamps speed to 0 during deceleration to stop, preventing this.
 *
 * [!] NO SERIAL PRINTS ON CORE 1:
 *   Serial.printf() takes ~8ms on Pico W. At the 500uss update rate of this
 *   module, even one print per second causes visible motor jitter. The
 *   diagnostic print block is commented out. Enable it for debugging ONLY.
 */

#include "simple_stepper.h"
#include "../drivers/mcp23017.h"
#include "../project_config.h" // For STEPPER_* constants
#include <Arduino.h>

// Motor 5 step/dir bit definitions for Port A
#ifdef ENABLE_MOTOR_5
#define M5_STEP_BIT (1 << 1) // GPA1
#define M5_DIR_BIT  (1 << 2) // GPA2
#endif

// Step/dir bit definitions for Port B
#define M1_DIR 0x01
#define M1_STEP 0x02
#define M2_DIR 0x04
#define M2_STEP 0x08
#define M3_DIR 0x10
#define M3_STEP 0x20
#define M4_DIR 0x40
#define M4_STEP 0x80

/**
 * @brief Per-motor state for the simple stepper engine.
 *
 * @details Each motor tracks three values:
 *   - targetSpeed: what Core 0 wants (from joystick via mutex)
 *   - currentSpeed: what the motor is actually doing (ramped)
 *   - accumulator: fractional step counter for timing
 */
struct MotorState {
  float targetSpeed;  /**< Commanded speed from joystick (steps/sec). */
  float currentSpeed; /**< Ramped speed with acceleration limit (steps/sec). */
  float accumulator;  /**< Fractional step accumulator for timing. */
};

#ifdef ENABLE_MOTOR_5
static MotorState motors[5] = {0};
#else
static MotorState motors[4] = {0};
#endif

/**
 * @brief Direction inversion array per motor.
 * @details Index matches motor number. -1 inverts a motor's direction.
 * FL and BL are inverted because they're mounted as mirror images of
 * FR and BR on a mecanum chassis. M5 inversion is user-configured.
 */
#ifdef ENABLE_MOTOR_5
static const int8_t DIR_INVERT[5] = {-1, 1, -1, 1, MOTOR_5_DIR_INVERT};
#else
static const int8_t DIR_INVERT[4] = {-1, 1, -1, 1};
#endif
static unsigned long lastUpdateTime = 0;

static bool motorsActive = false;

// Derived constants from config
// Updates per second = 1,000,000 / STEPPER_PULSE_INTERVAL_US
// Acceleration per update = STEPPER_ACCELERATION / updates_per_sec
static const float ACCEL_PER_UPDATE =
    STEPPER_ACCELERATION / (1000000.0f / STEPPER_PULSE_INTERVAL_US);

/**
 * @details Zeros all motor states and captures the initial timestamp.
 * Must be called from Core 1's setup1() (after the 2-second delay for
 * Core 0's MCP23017 init to complete).
 */
void simple_stepper_init() {
  lastUpdateTime = micros();
#ifdef ENABLE_MOTOR_5
  int numMotors = 5;
#else
  int numMotors = 4;
#endif
  for (int i = 0; i < numMotors; i++) {
    motors[i].targetSpeed = 0;
    motors[i].currentSpeed = 0;
    motors[i].accumulator = 0;
  }
  Serial.println("[SimpleStepper] Init with acceleration ramping");
#ifdef ENABLE_MOTOR_5
  Serial.println("[SimpleStepper] Motor 5 (Port A) enabled");
#endif
}

/**
 * @details On direction reversal, the accumulator is reset to zero but
 * currentSpeed is NOT reset. This lets the trapezoidal ramp decelerate
 * through zero smoothly rather than jumping (which would cause a
 * mechanical jerk). The zero-crossing detector in update() clamps speed
 * at zero during the transition.
 */
void simple_stepper_set_speed(uint8_t motor, float stepsPerSec) {
#ifdef ENABLE_MOTOR_5
  if (motor >= 5)
    return;
#else
  if (motor >= 4)
    return;
#endif

  float newSpeed = stepsPerSec * DIR_INVERT[motor];
  float oldSpeed = motors[motor].targetSpeed;

  // Detect direction reversal (signs opposite)
  bool signsOpposite =
      (newSpeed > 0 && oldSpeed < 0) || (newSpeed < 0 && oldSpeed > 0);

  if (signsOpposite) {
    motors[motor].accumulator = 0;
    // NOTE: Do NOT reset currentSpeed: let ramping decelerate through zero.
    // Resetting causes velocity discontinuity (jerk) at direction changes.
  }

  motors[motor].targetSpeed = newSpeed;
}

/**
 * @details Called from loop1() on Core 1. Rate-limited to
 * STEPPER_PULSE_INTERVAL_US (typically 500us = 2kHz).
 *
 * ALGORITHM (per motor, per update):
 *   1. Ramp currentSpeed toward targetSpeed by ACCEL_PER_UPDATE.
 *   2. Zero-crossing guard: if target is near zero and speed would
 *      cross zero, clamp to zero (prevents wrong-direction steps).
 *   3. Accumulate fractional steps: accumulator += |speed| * dt.
 *   4. If accumulator >= 1.0: this motor needs a step. Set its STEP bit.
 *   5. Build direction + step masks for all 4 motors.
 *
 * BATCH I2C WRITE (the key optimization):
 *   After processing all 4 motors, ONE setPortB(dir | step) writes both
 *   direction and step bits simultaneously. After 5us, a second setPortB(dir)
 *   clears the step bits. Total: 2 I2C writes for all 4 motors.
 */
void simple_stepper_update() {
  unsigned long now = micros();
  unsigned long elapsed = now - lastUpdateTime;

  if (elapsed < STEPPER_PULSE_INTERVAL_US) {
    return; // Not time yet: wait for the next interval
  }

  lastUpdateTime = now;

  float dt = elapsed / 1000000.0f;

  uint8_t stepMask = 0;  // Which motors need a step pulse this update
  uint8_t dirMask = 0;   // Direction bits: 1=forward, 0=reverse
  bool anyActive = false;

  for (int i = 0; i < 4; i++) {
    float target = motors[i].targetSpeed;
    float current = motors[i].currentSpeed;

    // Apply acceleration ramping (trapezoidal profile)
    float diff = target - current;
    if (fabsf(diff) > ACCEL_PER_UPDATE) {
      current += (diff > 0) ? ACCEL_PER_UPDATE : -ACCEL_PER_UPDATE;
    } else {
      current = target;
    }

    // CRITICAL: Prevent zero-crossing during deceleration.
    // If target is zero (joystick released) and we're decelerating,
    // clamp at zero. Without this, the motor briefly steps in the wrong
    // direction (audible as a "click").
    if (fabsf(target) < STEPPER_SPEED_DEADZONE) {
      if ((motors[i].currentSpeed > 0 && current <= 0) ||
          (motors[i].currentSpeed < 0 && current >= 0)) {
        current = 0; // Clamp at zero, don't cross
      }
    }

    motors[i].currentSpeed = current;

    float absSpeed = fabsf(current);

    // Skip motors below the deadzone threshold
    if (absSpeed < STEPPER_SPEED_DEADZONE) {
      motors[i].accumulator = 0;
      continue;
    }

    anyActive = true;

    // Set direction bit (1=forward). Reverse direction = bit stays 0.
    if (current > 0) {
      switch (i) {
      case 0:
        dirMask |= M1_DIR;
        break;
      case 1:
        dirMask |= M2_DIR;
        break;
      case 2:
        dirMask |= M3_DIR;
        break;
      case 3:
        dirMask |= M4_DIR;
        break;
      }
    }

    // Step accumulator: add fractional steps, pulse when >= 1.0
    motors[i].accumulator += absSpeed * dt;
    if (motors[i].accumulator >= 1.0f) {
      motors[i].accumulator -= 1.0f;
      if (motors[i].accumulator > 2.0f)
        motors[i].accumulator = 0; // Safety reset for accumulator overflow


      switch (i) {
      case 0:
        stepMask |= M1_STEP;
        break;
      case 1:
        stepMask |= M2_STEP;
        break;
      case 2:
        stepMask |= M3_STEP;
        break;
      case 3:
        stepMask |= M4_STEP;
        break;
      }
    }
  }

  // State tracking
  motorsActive = anyActive;

  // === BATCH I2C WRITE (2 writes for all 4 motors) ===
  // Write direction + step bits in one shot, wait 5us for minimum pulse
  // width, then clear step bits (keep direction). This is 4x faster than
  // per-motor stepperPulse() calls.
  if (stepMask != 0) {
    mcpStepper.setPortB(dirMask | stepMask); // I2C write 1: STEP HIGH + DIR
    delayMicroseconds(5);                     // TMC2209 min pulse: 100ns
    mcpStepper.setPortB(dirMask);             // I2C write 2: STEP LOW (done)
  }

  // === MOTOR 5: Port A step/dir (separate from Port B batch) ===
#ifdef ENABLE_MOTOR_5
  {
    MotorState &m5 = motors[4];
    float target = m5.targetSpeed;
    float current = m5.currentSpeed;

    // Acceleration ramp
    float diff = target - current;
    if (fabsf(diff) > ACCEL_PER_UPDATE) {
      current += (diff > 0) ? ACCEL_PER_UPDATE : -ACCEL_PER_UPDATE;
    } else {
      current = target;
    }

    // Zero-crossing guard
    if (fabsf(target) < STEPPER_SPEED_DEADZONE) {
      if ((m5.currentSpeed > 0 && current <= 0) ||
          (m5.currentSpeed < 0 && current >= 0)) {
        current = 0;
      }
    }

    m5.currentSpeed = current;
    float absSpeed = fabsf(current);

    if (absSpeed >= STEPPER_SPEED_DEADZONE) {
      // Set direction
      mcpStepper.setBitA(M5_DIR_BIT, current > 0);

      // Step accumulator
      m5.accumulator += absSpeed * dt;
      if (m5.accumulator >= 1.0f) {
        m5.accumulator -= 1.0f;
        if (m5.accumulator > 2.0f)
          m5.accumulator = 0;

        // Step pulse on Port A
        mcpStepper.setBitA(M5_STEP_BIT, true);   // I2C write 3
        delayMicroseconds(5);                     // TMC2209 min pulse
        mcpStepper.setBitA(M5_STEP_BIT, false);  // I2C write 4
      }
    } else {
      m5.accumulator = 0;
    }
  }
#endif
}

/**
 * @details Immediately zeros all motor state (no acceleration ramp) and
 * drives Port B LOW to ensure no step or direction pins are left HIGH.
 * Called from Core 1 when it reads the g_emergencyStop flag.
 */
void simple_stepper_stop_all() {
#ifdef ENABLE_MOTOR_5
  int numMotors = 5;
#else
  int numMotors = 4;
#endif
  for (int i = 0; i < numMotors; i++) {
    motors[i].targetSpeed = 0;
    motors[i].currentSpeed = 0;
    motors[i].accumulator = 0;
  }
  mcpStepper.setPortB(0); // All Port B pins LOW: M1-M4 off
#ifdef ENABLE_MOTOR_5
  mcpStepper.setBitA(M5_STEP_BIT, false); // M5 step LOW
  mcpStepper.setBitA(M5_DIR_BIT, false);  // M5 dir LOW
#endif
  motorsActive = false;
}
