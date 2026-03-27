/**
 * @file motor_manager.h
 * @brief Central motor registry: init, stop, and access for all motors.
 *
 * WHAT THIS MODULE DOES:
 *   Think of motor_manager as the "front desk" for all motors. It creates
 *   the motor objects at boot, keeps them in an array, and provides simple
 *   functions to set speeds or stop everything. The rest of the code never
 *   talks to individual motor objects directly; it goes through here.
 *
 * INIT SEQUENCE (order matters!):
 *   1. I2C bus init (Wire.begin)
 *   2. MCP23017 init (GPIO expander chip)
 *   3. Microstepping pin configuration
 *   4. motors_init() -- this module
 *
 *   motors_init() must be called BEFORE BLE starts, because once BLE
 *   takes over Core 0's scheduling, initialization code may not run
 *   reliably.
 *
 * MULTICORE I2C CAVEAT:
 *   motors_stop_all() writes to MCP23017 from Core 0. If Core 1 is also
 *   writing (simple_stepper), I2C bus contention can occur. This is
 *   acceptable for emergency stop (safety is more important than timing
 *   precision).
 *
 * @see motor_base.h for the abstract motor interface
 * @see motor_dc.h for DC motor implementation
 * @see motor_stepper.h for stepper motor implementation
 * @see project_config.h for motor configuration values
 */

#ifndef MOTOR_MANAGER_H
#define MOTOR_MANAGER_H

#include "motor_base.h"
#include <stdint.h>

/** @brief Maximum number of motors the system supports.
 *
 *  A standard 4-wheel mecanum robot uses 4 motors (indices 0-3).
 *  This value sets the size of the internal motor array.
 */
#define NUM_MOTORS 4

/**
 * @brief Initialize all motors based on the settings in project_config.h.
 *
 * Creates MotorDC or MotorStepper objects depending on the configured
 * MOTOR_TYPE, configures their hardware (GPIO pins, MCP23017 expander),
 * and ensures all motors start in a stopped state.
 *
 * @return true if all enabled motors initialized successfully.
 * @return false if any motor failed to initialize (e.g., invalid pin
 *         number, MCP23017 communication error). Failed motors are
 *         disabled but the system continues to function with the
 *         remaining motors.
 *
 * @warning Must be called before ble_init(). BLE takes over Core 0's
 *          scheduling, so late initialization may not complete.
 * @note For steppers, this also configures the MCP23017 microstepping
 *       pins and enables the stepper driver outputs.
 * @see project_config.h for motor pin assignments and configuration
 */
bool motors_init();

/**
 * @brief Get a motor by its index number.
 *
 * Returns a pointer to the motor object at the given index. Use this
 * to access type-specific functions (e.g., cast to MotorDC for setTarget()
 * or MotorStepper for setTargetSpeed()).
 *
 * @param index Motor index (0-3). 0=FL, 1=FR, 2=BL, 3=BR.
 * @return Pointer to the MotorBase object, or nullptr if the index is
 *         out of range or the motor was not created.
 *
 * @see MotorBase::getType() to check if it's DC or Stepper before casting
 */
MotorBase *motor_get(int index);

/**
 * @brief Set the PWM speed for a DC motor.
 *
 * Convenience function that looks up the motor by index, verifies it's a
 * DC motor, casts to MotorDC, and calls setTarget(). If the motor is not
 * DC or the index is invalid, the call is silently ignored.
 *
 * @param index Motor index (0-3).
 * @param pwm   PWM value from -255 (full reverse) to +255 (full forward).
 *
 * @see MotorDC::setTarget() for detailed behavior
 */
void motor_set_pwm(int index, int16_t pwm);

/**
 * @brief Set the target speed for a stepper motor.
 *
 * Convenience function that looks up the motor by index, verifies it's a
 * stepper, casts to MotorStepper, and calls setTargetSpeed(). If the motor
 * is not a stepper or the index is invalid, the call is silently ignored.
 *
 * @param index      Motor index (0-3).
 * @param stepsPerSec Target speed in steps per second. Positive = forward,
 *                    negative = reverse.
 *
 * @see MotorStepper::setTargetSpeed() for detailed behavior
 */
void motor_set_speed(int index, float stepsPerSec);

/**
 * @brief Update all motor control loops.
 *
 * Calls update() on each enabled motor. For DC motors, this applies PWM
 * changes. For steppers, this advances the trapezoidal acceleration ramp
 * and may generate step pulses.
 *
 * @param dtSec Time since the last call, in seconds. Typically 0.02
 *              (50Hz update rate from main loop).
 *
 * @note Called automatically from main.cpp's loop(). You don't need to
 *       call this directly unless building a custom main loop.
 */
void motors_update(float dtSec);

/**
 * @brief Emergency stop all motors immediately.
 *
 * Calls stop() on every motor instance. For DC motors, sets PWM to zero.
 * For steppers, zeroes speed and halts step pulses. No ramping, immediate
 * stop.
 *
 * @warning This function writes to MCP23017 from Core 0. If Core 1's
 *          simple_stepper is also writing simultaneously, a brief I2C bus
 *          conflict may occur. This is acceptable: safety trumps timing.
 *
 * @note Called automatically by the safety watchdog when BLE connection
 *       is lost, and on explicit disconnect.
 * @see safety_check_timeout() which triggers this on timeout
 * @see simple_stepper_stop_all() for the Core 1 equivalent
 */
void motors_stop_all();

/**
 * @brief Check if any motors in the system are steppers.
 *
 * Used by main.cpp to decide whether to start Core 1's simple_stepper
 * engine. If all motors are DC (no steppers), Core 1 stays idle and
 * simple_stepper is never initialized.
 *
 * @return true if at least one enabled motor is MotorType::Stepper.
 * @return false if all motors are DC or no motors are enabled.
 */
bool motors_has_steppers();

/**
 * @brief Check if any motors in the system are DC.
 *
 * Used by the motion profile to determine whether to set PWM values
 * (DC) or step speeds (stepper) in the command handler.
 *
 * @return true if at least one enabled motor is MotorType::DC.
 * @return false if all motors are steppers or no motors are enabled.
 */
bool motors_has_dc();

#endif // MOTOR_MANAGER_H
