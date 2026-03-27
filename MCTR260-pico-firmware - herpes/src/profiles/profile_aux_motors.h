/**
 * @file profile_aux_motors.h
 * @brief Auxiliary DC motor profile: on/off direction control via MCP23017.
 *
 * DC Motors 3 and 4 on MCP23017 U6_2 (address 0x21) Port A are controlled
 * by aux slider inputs from the Flutter app. Direction is set via H-bridge
 * pins; speed control is not available over I2C (on/off only).
 *
 * @see project_config.h for ENABLE_DC_MOTOR_3 / ENABLE_DC_MOTOR_4
 * @see mcp23017.h for the I2C GPIO expander driver
 */

#ifndef PROFILE_AUX_MOTORS_H
#define PROFILE_AUX_MOTORS_H

#include "core/command_packet.h"

/**
 * @brief Apply aux channel inputs to DC Motors 3 and 4.
 *
 * Reads the configured aux slider channels and sets H-bridge direction
 * pins on MCP23017 U6_2 Port A. Motor behavior:
 *   - Aux > +5:  Forward (positive pin HIGH, negative pin LOW)
 *   - Aux < -5:  Reverse (positive pin LOW, negative pin HIGH)
 *   - |Aux| <= 5: Off (both pins LOW, motor brakes to stop)
 *
 * @param cmd Parsed control command from BLE.
 *
 * @note Runs on Core 0. MCP23017 U6_2 (0x21) is not accessed by Core 1,
 *       so no mutex protection is needed for these I2C writes.
 */
void profile_aux_motors_apply(const control_command_t *cmd);

/**
 * @brief Emergency stop: turn off all auxiliary DC motors.
 *
 * Sets both H-bridge pins LOW for Motors 3 and 4. Called on BLE
 * disconnect and safety timeout to ensure motors stop immediately.
 */
void profile_aux_motors_stop();

#endif // PROFILE_AUX_MOTORS_H
