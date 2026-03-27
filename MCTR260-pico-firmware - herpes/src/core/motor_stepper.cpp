/**
 * @file motor_stepper.cpp
 * @brief Stepper motor control with trapezoidal acceleration (Core 0 version)
 *
 * TRAPEZOIDAL ACCELERATION PROFILE:
 *   Instead of jumping instantly to the target speed (which causes missed
 *   steps and motor stall), this module ramps speed linearly:
 *
 *     Speed |    /-------\
 *           |   /  cruise  \
 *           |  /             \
 *     0 ----+-/---------------\--> Time
 *           | accel    decel
 *
 *   The acceleration rate is set by STEPPER_ACCELERATION in project_config.h.
 *
 * NOTE: This is the OBJECT-ORIENTED stepper class used by motor_manager.
 *   There is also simple_stepper.cpp, a lightweight procedural version
 *   that runs on Core 1 with batch I2C writes. In the current architecture,
 *   simple_stepper is the one actually generating pulses at runtime.
 *   This class is retained as the full-featured alternative for position
 *   mode, homing, and per-motor acceleration if needed in the future.
 *
 * MCP23017:
 *   Step/dir pins are NOT on Pico GPIO; they're on an MCP23017 I2C GPIO
 *   expander. Each stepperPulse() call involves 2 I2C writes (set high, set
 * low).
 */

#include "motor_stepper.h"
#include "../drivers/mcp23017.h"
#include <Arduino.h>

// Minimum step interval to prevent going too fast (10 microseconds)
#define MIN_STEP_INTERVAL_US 10

// =============================================================================
// CONSTRUCTOR
// =============================================================================

/**
 * @details Member initializer list sets all state to zero/safe defaults.
 * No hardware access happens here; init() does that.
 */
MotorStepper::MotorStepper(const MotorStepperConfig &config)
    : cfg_(config), mode_(StepperMode::Velocity), currentPosition_(0),
      targetPosition_(0), targetSpeed_(0), currentSpeed_(0), lastStepTime_(0),
      stepInterval_(0), lastDirection_(true) {}

// =============================================================================
// MOTOR BASE INTERFACE
// =============================================================================

/**
 * @details For MechaPico MCB: the MCP23017 I2C expander is initialized globally
 * in motors_init() before any motor's init() is called. This function only
 * validates that the motor index is within the MCP23017's range (0-4 for M1-M5)
 * and resets the step timing to prevent a burst of steps on first motion.
 */
bool MotorStepper::init() {
  if (!cfg_.enabled) {
    return true;
  }

  // For MechaPico MCB: MCP23017 is initialized separately in motors_init()
  // Individual motor init just needs to verify valid motor index
  if (cfg_.index >= 5) {
    Serial.printf("[MotorStepper] ERROR: Invalid motor index %d (max 4)\n",
                  cfg_.index);
    return false;
  }

  Serial.printf(
      "[MotorStepper] Motor %d initialized (%s, %d microsteps, MCP23017)\n",
      cfg_.index,
      cfg_.driverType == StepperDriverType::TMC2209 ? "TMC2209"
      : cfg_.driverType == StepperDriverType::A4988 ? "A4988"
                                                    : "DRV8825",
      cfg_.microstepping);

  // Initialize timing to current time to prevent burst on first motion
  lastStepTime_ = micros();

  return true;
}

/**
 * @details Zeroes both target and current speed immediately (no ramping).
 * Position is intentionally preserved so the motor "knows" where it
 * stopped for future position-mode moves.
 */
void MotorStepper::stop() {
  targetSpeed_ = 0;
  currentSpeed_ = 0;
  stepInterval_ = 0;
  mode_ = StepperMode::Velocity;

  // Keep current position (don't reset)
}

/**
 * @details Main control loop, called at ~50Hz from main loop.
 *
 * TWO-PHASE UPDATE:
 * Phase 1 (Acceleration): Ramps currentSpeed toward targetSpeed using
 *   the configured acceleration rate. In position mode, it also computes
 *   deceleration distance using v^2 = 2*a*d to know when to start slowing.
 *
 * Phase 2 (Step generation): Converts speed to a step interval in
 *   microseconds and generates step pulses via MCP23017. A while-loop
 *   catches up on missed steps if the update is late (e.g., dtSec > 1/speed).
 *   A safety limit of 200 steps per update prevents runaway in error cases.
 *
 * KEY DETAIL: lastStepTime_ is incremented by stepInterval_, NOT set
 * to "now". This avoids accumulated timing drift at high speeds.
 */
void MotorStepper::update(float dtSec) {
  if (!cfg_.enabled) {
    return;
  }

  // ==========================================================================
  // ACCELERATION RAMPING
  // ==========================================================================

  float targetSpeedAbs;

  if (mode_ == StepperMode::Velocity) {
    targetSpeedAbs = targetSpeed_;
  } else {
    // Position mode: calculate speed needed to reach target
    int32_t stepsRemaining = targetPosition_ - currentPosition_;

    if (stepsRemaining == 0) {
      targetSpeedAbs = 0;
    } else {
      // Calculate deceleration distance using kinematic equation:
      //   v^2 = 2 * a * d  =>  d = v^2 / (2*a)
      float decelDistance =
          (currentSpeed_ * currentSpeed_) / (2.0f * cfg_.acceleration);

      if (abs(stepsRemaining) <= decelDistance) {
        // Close enough to target: start decelerating
        targetSpeedAbs = 0;
      } else {
        // Far from target: accelerate or cruise at max speed
        targetSpeedAbs = (stepsRemaining > 0) ? cfg_.maxSpeed : -cfg_.maxSpeed;
      }
    }
  }

  // Ramp current speed toward target (trapezoidal profile)
  float speedDiff = targetSpeedAbs - currentSpeed_;
  float maxChange = cfg_.acceleration * dtSec;

  if (fabsf(speedDiff) <= maxChange) {
    currentSpeed_ = targetSpeedAbs;
  } else if (speedDiff > 0) {
    currentSpeed_ += maxChange;
  } else {
    currentSpeed_ -= maxChange;
  }

  // Clamp to max speed
  if (currentSpeed_ > cfg_.maxSpeed)
    currentSpeed_ = cfg_.maxSpeed;
  if (currentSpeed_ < -cfg_.maxSpeed)
    currentSpeed_ = -cfg_.maxSpeed;

  // ==========================================================================
  // STEP GENERATION
  // ==========================================================================

  if (fabsf(currentSpeed_) < 1.0f) {
    // Stopped: reset timing for clean start when motion resumes
    stepInterval_ = 0;
    lastStepTime_ = micros(); // Reset to prevent stale delta on resume
    return;
  }

  calculateStepInterval();

  // Generate all pending steps (may be multiple per update at high speeds)
  // Limit iterations to prevent infinite loop if timing is off
  unsigned long now = micros();
  int stepsThisUpdate = 0;
  const int maxStepsPerUpdate = 200; // Safety limit


  while (stepInterval_ > 0 && (now - lastStepTime_) >= stepInterval_) {
    generateStep();
    lastStepTime_ += stepInterval_; // Increment by interval, not current time
    stepsThisUpdate++;

    if (stepsThisUpdate >= maxStepsPerUpdate) {
      // Prevent runaway: reset timing baseline
      lastStepTime_ = now;
      break;
    }
  }

}

uint8_t MotorStepper::getIndex() const { return cfg_.index; }

bool MotorStepper::isEnabled() const { return cfg_.enabled; }

MotorType MotorStepper::getType() const { return MotorType::Stepper; }

// =============================================================================
// STEPPER SPECIFIC
// =============================================================================

void MotorStepper::setTargetSpeed(float stepsPerSec) {
  mode_ = StepperMode::Velocity;
  targetSpeed_ = stepsPerSec * cfg_.direction;

  // Clamp to max speed
  if (targetSpeed_ > cfg_.maxSpeed)
    targetSpeed_ = cfg_.maxSpeed;
  if (targetSpeed_ < -cfg_.maxSpeed)
    targetSpeed_ = -cfg_.maxSpeed;
}

void MotorStepper::moveTo(int32_t targetSteps) {
  mode_ = StepperMode::Position;
  targetPosition_ = targetSteps * cfg_.microstepping;
}

void MotorStepper::moveRelative(int32_t steps) {
  moveTo((currentPosition_ / cfg_.microstepping) + steps);
}

int32_t MotorStepper::getPosition() const {
  return currentPosition_ / cfg_.microstepping;
}

float MotorStepper::getCurrentSpeed() const {
  return currentSpeed_ / cfg_.microstepping;
}

bool MotorStepper::isMoving() const { return fabsf(currentSpeed_) >= 1.0f; }

void MotorStepper::setHome() {
  currentPosition_ = 0;
  targetPosition_ = 0;
}

// =============================================================================
// PRIVATE METHODS
// =============================================================================

/**
 * @details Converts steps/sec to microseconds/step. At 1000 steps/sec,
 * interval = 1,000,000 / 1000 = 1000us = 1ms between steps. The minimum
 * interval clamp prevents the driver from receiving pulses faster than
 * it can process (TMC2209 minimum pulse width is ~100ns, but I2C latency
 * dominates at ~50us per step).
 */
void MotorStepper::calculateStepInterval() {
  if (fabsf(currentSpeed_) < 1.0f) {
    stepInterval_ = 0;
    return;
  }

  // Convert steps/sec to microseconds/step
  stepInterval_ = (unsigned long)(1000000.0f / fabsf(currentSpeed_));

  // Enforce minimum interval
  if (stepInterval_ < MIN_STEP_INTERVAL_US) {
    stepInterval_ = MIN_STEP_INTERVAL_US;
  }
}

/**
 * @details Sets direction (only on change, to save I2C bandwidth) then
 * generates a step pulse via the MCP23017's stepperPulse() function.
 *
 * The direction inversion uses cfg_.direction: if the motor is physically
 * mounted backwards (common on mecanum robots where left and right sides
 * mirror each other), cfg_.direction = -1 inverts the mapping so
 * "positive speed" always means "robot forward".
 */
void MotorStepper::generateStep() {
  // Set direction via MCP23017 (only if changed: saves I2C bandwidth)
  bool forward =
      (currentSpeed_ >= 0) ? (cfg_.direction > 0) : (cfg_.direction < 0);
  if (forward != lastDirection_) {
    stepperSetDirection(cfg_.index, forward);
    lastDirection_ = forward;
  }

  // Generate step pulse via MCP23017 (2 I2C writes: HIGH then LOW)
  stepperPulse(cfg_.index);

  // Update position counter
  if (currentSpeed_ >= 0) {
    currentPosition_++;
  } else {
    currentPosition_--;
  }
}
