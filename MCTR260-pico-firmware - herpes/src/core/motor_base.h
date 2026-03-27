/**
 * @file motor_base.h
 * @brief Abstract base class for all motor types (DC, stepper, servo).
 *
 * WHAT IS AN ABSTRACT BASE CLASS?
 *   This file defines a "template" that all motor types must follow. It
 *   lists the functions every motor must have (init, stop, update) but
 *   doesn't implement them. Each specific motor type (DC or stepper)
 *   provides its own implementation. This is C++ polymorphism: the rest
 *   of the firmware calls motor->stop() without needing to know whether
 *   it's a DC motor or a stepper underneath.
 *
 * CLASS HIERARCHY:
 *   MotorBase (this abstract class)
 *   +-- MotorDC:      PWM control via H-bridge (DRV8871, DRV8833, L298N)
 *   +-- MotorStepper: Step/dir control via MCP23017 (TMC2209, A4988, DRV8825)
 *
 * WHY setTarget() IS NOT IN THE BASE CLASS:
 *   DC motors take a PWM value (-255 to +255).
 *   Steppers take a speed in steps/sec (float).
 *   Servos would take an angle (0-180 degrees).
 *   Forcing these into one function signature would require ugly workarounds.
 *   Instead, motor_manager.cpp casts to the specific subclass when needed.
 *
 * @see motor_dc.h for the DC motor implementation
 * @see motor_stepper.h for the stepper motor implementation
 * @see motor_manager.h for the registry that holds all motor instances
 */

#ifndef MOTOR_BASE_H
#define MOTOR_BASE_H

#include <stdint.h>

/**
 * @brief Motor type enumeration.
 *
 * Used by motor_manager and the motion profile to determine what kind of
 * motor is at a given index, so it can cast to the right subclass and
 * call type-specific functions (e.g., setTarget for DC vs. setTargetSpeed
 * for stepper).
 */
enum class MotorType {
  DC,      /**< DC motor with PWM speed control. @see MotorDC */
  Stepper, /**< Stepper motor with step/dir control. @see MotorStepper */
  Servo    /**< Future: servo motor with angle control. Not yet implemented. */
};

/**
 * @brief Abstract base class for all motor types.
 *
 * Defines the interface that every motor must implement. The motion profile
 * and motor manager work with MotorBase pointers, so they don't need to
 * know (or care) whether the underlying hardware is a DC motor or stepper.
 *
 * @see MotorDC for the concrete DC motor implementation
 * @see MotorStepper for the concrete stepper implementation
 */
class MotorBase {
public:
  virtual ~MotorBase() = default;

  /**
   * @brief Initialize the motor hardware.
   *
   * Called once during boot by motors_init(). Sets up GPIO pins, PWM
   * frequency, or MCP23017 registers depending on the motor type.
   *
   * @return true if hardware initialization succeeded and the motor is
   *         ready to use.
   * @return false if initialization failed (e.g., invalid pin number,
   *         MCP23017 communication error). The motor will be disabled.
   *
   * @note Called once during motors_init(). Do not call again after boot.
   * @see motors_init() in motor_manager.h
   */
  virtual bool init() = 0;

  /**
   * @brief Stop the motor immediately (emergency stop).
   *
   * Sets speed/PWM to zero with no ramping. For DC motors, this means
   * PWM = 0 (coast). For steppers, this means zero speed and no more
   * step pulses. Used by the safety watchdog when connection is lost.
   *
   * @note This does NOT disable the motor driver hardware. The motor
   *       is still energized and ready to move again when commanded.
   * @see motors_stop_all() which calls this for every motor
   */
  virtual void stop() = 0;

  /**
   * @brief Update the motor's control loop (call at 50Hz).
   *
   * For DC motors: applies PWM smoothing (if implemented).
   * For steppers: updates the acceleration ramp and generates step pulses
   * through the MCP23017 I2C GPIO expander.
   *
   * @param dtSec Time since the last update() call, in seconds. Typically
   *              0.02 (= 1/50Hz). Used by steppers to calculate how much
   *              to ramp the speed this cycle.
   *
   * @note Called automatically by motors_update() in the main loop.
   *       You don't need to call this directly.
   * @see motors_update() in motor_manager.h
   */
  virtual void update(float dtSec) = 0;

  /**
   * @brief Get this motor's index number (0-based).
   *
   * The index identifies which physical motor this is: 0 = front-left,
   * 1 = front-right, 2 = back-left, 3 = back-right (for a 4-motor robot).
   *
   * @return Motor index, 0-3 for a standard 4-motor configuration.
   */
  virtual uint8_t getIndex() const = 0;

  /**
   * @brief Check if this motor is enabled in the configuration.
   *
   * A disabled motor has its `enabled` config flag set to false. It will
   * be skipped during init and update cycles. Useful for testing with
   * fewer than 4 motors connected.
   *
   * @return true if the motor is enabled and actively controlled.
   * @return false if the motor is disabled in project_config.h.
   */
  virtual bool isEnabled() const = 0;

  /**
   * @brief Get what kind of motor this is (DC, Stepper, or Servo).
   *
   * Used by motor_manager to cast the MotorBase pointer to the correct
   * subclass when calling type-specific functions like setTarget() (DC)
   * or setTargetSpeed() (stepper).
   *
   * @return The MotorType enum value for this motor.
   * @see MotorType for the possible values
   */
  virtual MotorType getType() const = 0;
};

#endif // MOTOR_BASE_H
