/**
 * @file profile_mecanum.h
 * @brief Mecanum drive profile: joystick commands -> kinematics -> motor
 * speeds.
 *
 * WHAT IS A "PROFILE"?
 *   A profile is a translation layer between generic app commands and
 *   specific motor behavior. The app doesn't know whether your robot is
 *   a mecanum, a tank drive, or a forklift. It just sends joystick data.
 *   The profile reads that data and figures out what each motor should do.
 *
 * PIPELINE FOR EACH COMMAND:
 *   1. App sends JSON: {left: {x, y}, right: {x, y}, speed: 80}
 *   2. command_parser.cpp fills a control_command_t struct
 *   3. THIS MODULE reads the struct and calls mecanum_calculate()
 *   4. mecanum_calculate() does the inverse kinematics math
 *   5. The resulting wheel speeds are sent to the motors:
 *      - For DC motors: motor_set_pwm() on Core 0
 *      - For steppers: written to g_targetSpeeds[] behind a mutex,
 *        then picked up by simple_stepper on Core 1
 *
 * MULTICORE HANDOFF (steppers only):
 *   Motor speeds are calculated here on Core 0, but stepper pulses are
 *   generated on Core 1 (for timing precision). The handoff uses a
 *   mutex-protected shared array (g_targetSpeeds[]). Core 0 writes the
 *   new speeds; Core 1 reads them in its 500us loop. The mutex prevents
 *   Core 1 from reading half-written data.
 *
 * @see mecanum_kinematics.h for the IK math
 * @see command_packet.h for the control_command_t struct
 * @see simple_stepper.h for the Core 1 stepper engine
 * @see motor_manager.h for motor_set_pwm() (DC motors)
 */

#ifndef PROFILE_MECANUM_H
#define PROFILE_MECANUM_H

#include "core/command_packet.h"

/**
 * @brief Apply the mecanum drive profile to a control command.
 *
 * Reads joystick/dial inputs from the command struct, runs inverse
 * kinematics, and writes the resulting speeds to the 4 motors.
 *
 * For a mecanum robot, by default:
 *   - Left input (dial): rotation (omega, clockwise/counter-clockwise)
 *   - Right input (joystick): translation (vx = strafe, vy = forward)
 *   - Speed slider: scales all outputs proportionally
 *
 * @param cmd Pointer to the control command struct from the parser.
 *            Must not be null. Must have type "control" (heartbeats
 *            are ignored before reaching this function).
 *
 * @note For steppers, the computed speeds are written behind a mutex
 *       to g_targetSpeeds[]. Core 1 picks them up asynchronously.
 * @note For DC motors, motor_set_pwm() is called directly on Core 0.
 *
 * @see mecanum_calculate() for the IK equations
 * @see control_command_t for the input data format
 */
void profile_mecanum_apply(const control_command_t *cmd);

#endif // PROFILE_MECANUM_H
