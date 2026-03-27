/**
 * @file simple_stepper.h
 * @brief Core 1 real-time stepper engine: flat C API for direct MCP23017
 * control
 *
 * WHY THIS EXISTS ALONGSIDE MotorStepper:
 *   MotorStepper (OOP) does 8 I2C writes for 4 motors (2 per motor).
 *   simple_stepper batches all 4 motors into 2 I2C writes total.
 *   On Core 1's tight 500us loop, this 4x reduction is critical.
 *
 * HOW IT WORKS (for beginners):
 *   Think of this module as a metronome for your stepper motors. Every 500
 *   microseconds, it checks each motor's target speed and decides whether
 *   it's time to send a "step" pulse. Faster speed = more frequent pulses.
 *   It also ramps speed up and down gradually (acceleration) so the motor
 *   doesn't skip or stall from sudden speed changes.
 *
 * THREAD SAFETY: All functions here run ONLY on Core 1.
 *   Core 0 communicates via g_targetSpeeds[] protected by g_speedMutex.
 *
 * @see motor_stepper.h for the OOP stepper class (position mode, per-motor
 *      acceleration). That class runs on Core 0; this module runs on Core 1.
 * @see mcp23017.h for the I2C GPIO expander that physically drives the motors.
 */

#ifndef SIMPLE_STEPPER_H
#define SIMPLE_STEPPER_H

#include <stdint.h>

/**
 * @brief Initialize the Core 1 stepper engine.
 *
 * Sets up internal state (accumulators, speed ramps) for 4 stepper motors.
 * Must be called from setup1() on Core 1 before the main loop1() starts.
 *
 * @note This does NOT initialize the MCP23017 hardware. That is done
 *       separately by motors_init() on Core 0 during boot.
 * @warning Do NOT call from Core 0. Core 1 owns this module's timing state.
 * @see simple_stepper_update() for the main pulse-generation loop
 */
void simple_stepper_init();

/**
 * @brief Set the target speed for one motor.
 *
 * The motor won't jump to this speed instantly. Instead,
 * simple_stepper_update() will ramp the actual speed up or down according
 * to the acceleration limit defined in project_config.h. This prevents the
 * motor from skipping steps.
 *
 * @param motor  Motor index (0-3), where 0=Front-Left (M1), 1=Front-Right
 *               (M2), 2=Back-Left (M3), 3=Back-Right (M4).
 * @param stepsPerSec  Target speed in steps per second. Positive = forward,
 *                     negative = reverse. The direction inversion for
 *                     physically mirrored motors is handled internally.
 *
 * @note Called from Core 0 (via the mutex-protected handoff in
 *       profile_mecanum.cpp). Safe to call while update() is running
 *       because each motor's targetSpeed is written atomically.
 * @see profile_mecanum_apply() which calculates and sets these speeds
 */
void simple_stepper_set_speed(uint8_t motor, float stepsPerSec);

/**
 * @brief Generate step pulses for all 4 motors (call every 500us from
 * loop1).
 *
 * This is the heart of the stepper engine. Each call:
 *   1. Ramps each motor's current speed toward its target (acceleration).
 *   2. Accumulates fractional steps (speed x elapsed time).
 *   3. When a full step accumulates, sets the direction and step bits.
 *   4. Writes ALL motors' direction + step bits to MCP23017 Port B in
 *      a single I2C transaction (2 writes total: pulse HIGH, then LOW).
 *
 * @note This function is time-critical. It must complete within ~200us
 *       to leave headroom in the 500us loop. Do NOT add Serial.printf()
 *       calls here. Serial output on Pico W takes ~8ms and will cause
 *       visible motor jitter.
 * @warning Must only be called from Core 1's loop1(). Calling from Core 0
 *          would corrupt internal timing state.
 * @see simple_stepper_init() must be called first in setup1()
 */
void simple_stepper_update();

/**
 * @brief Immediately stop all 4 motors (zero speed, zero pulses).
 *
 * Clears all internal state (speed, accumulators) and writes 0x00 to
 * MCP23017 Port B, which de-asserts all step and direction signals.
 *
 * @note This is called on BLE disconnect and safety timeout. It does NOT
 *       ramp down. The motors stop instantly. This is intentional for
 *       emergency stop scenarios (e.g., your robot is about to drive
 *       off a table).
 * @see safety_check_timeout() which triggers this on lost connection
 * @see motors_stop_all() which is the Core 0 equivalent
 */
void simple_stepper_stop_all();

#endif // SIMPLE_STEPPER_H
