/**
 * @file mecanum_kinematics.h
 * @brief Mecanum wheel inverse kinematics: (vx, vy, omega) -> 4 wheel speeds.
 *
 * WHAT IS INVERSE KINEMATICS?
 *   You want the robot to move forward, strafe right, and rotate clockwise
 *   at the same time. But the robot has 4 individual wheels. Inverse
 *   kinematics (IK) is the math that converts your desired motion (forward,
 *   sideways, rotation) into individual wheel speeds that produce that
 *   motion.
 *
 * HOW MECANUM WHEELS WORK:
 *   Mecanum wheels have diagonal rollers (angled at 45 degrees from the
 *   wheel axis). When all 4 wheels spin forward, the robot moves forward
 *   (the diagonal forces cancel sideways). But when you spin wheels in
 *   specific patterns, the diagonal forces ADD UP instead of cancelling,
 *   producing sideways or rotational movement.
 *
 *   Pattern examples:
 *     All forward:          robot drives forward
 *     FL+BR fwd, FR+BL rev: robot strafes right
 *     Left fwd, right rev:  robot rotates clockwise
 *
 * COORDINATE CONVENTION:
 *   vx  = strafe (positive = right)
 *   vy  = forward/backward (positive = forward)
 *   omega = rotation (positive = clockwise, when viewed from above)
 *
 * See mecanum_kinematics.cpp for the full equations and normalization logic.
 * Roller angle convention: 45 degrees from wheel axis (standard X-config).
 *
 * @see profile_mecanum.h which calls mecanum_calculate()
 * @see CONFIGURATION_REFERENCE.md for vehicle geometry settings
 */

#ifndef MECANUM_KINEMATICS_H
#define MECANUM_KINEMATICS_H

/**
 * @brief Wheel speeds for a 4-wheel mecanum drive.
 *
 * Contains the computed speed for each wheel after inverse kinematics.
 * Values are normalized to the range [-1.0, +1.0], where 1.0 represents
 * maximum motor speed in that direction.
 *
 * @note The wheel naming follows the standard convention when looking
 *       DOWN at the robot from above:
 *       FL (front-left) | FR (front-right)
 *       BL (back-left)  | BR (back-right)
 */
struct WheelSpeeds {
  float frontLeft;  /**< Front-left wheel speed (-1 to +1). Positive = forward
                         rotation. */
  float frontRight; /**< Front-right wheel speed (-1 to +1). Positive = forward
                         rotation. */
  float backLeft;   /**< Back-left wheel speed (-1 to +1). Positive = forward
                         rotation. */
  float backRight;  /**< Back-right wheel speed (-1 to +1). Positive = forward
                         rotation. */
};

/**
 * @brief Calculate individual wheel speeds from desired robot motion.
 *
 * This is the core inverse kinematics function. Given how you want the
 * robot to move (forward, sideways, rotate), it computes how fast each
 * of the 4 wheels should spin.
 *
 * @param vx              Strafe velocity. -100 = full left, +100 = full right,
 *                        0 = no sideways motion.
 * @param vy              Forward/backward velocity. +100 = full forward,
 *                        -100 = full backward.
 * @param omega           Rotation velocity. +100 = full clockwise rotation,
 *                        -100 = full counter-clockwise. When viewed from above.
 * @param speedMultiplier Speed scaling factor (0.0 to 1.0). This comes from
 *                        the app's speed slider. 1.0 = full speed, 0.5 = half.
 * @param output          Pointer to the WheelSpeeds struct to fill with the
 *                        computed wheel speeds. Must not be null.
 *
 * @note Deadzone filtering (ignoring tiny joystick movements near center)
 *       is handled by the Flutter app at the input level, not here.
 * @note If the raw IK calculation produces a wheel speed greater than 1.0
 *       (possible when combining strafe + rotate), all wheels are scaled
 *       down proportionally so no wheel exceeds 1.0. This preserves the
 *       desired motion direction while staying within motor limits.
 *
 * @see profile_mecanum_apply() which calls this with joystick data
 * @see WheelSpeeds for the output structure
 */
void mecanum_calculate(float vx, float vy, float omega, float speedMultiplier,
                       WheelSpeeds *output);

#endif // MECANUM_KINEMATICS_H
