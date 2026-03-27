/**
 * @file profile_aux_motors.cpp
 * @brief Auxiliary motor profile: Motor 5 (stepper) and DC Motors 3-4.
 *
 * This profile handles all auxiliary motors that are independent of the
 * main drive system (mecanum M1-M4). It runs on Core 0 after the main
 * motion profile and maps aux slider inputs to motor actions.
 *
 * MOTOR 5 (STEPPER):
 *   Motor 5 is on MCP23017 U6_1 Port A (GPA1=STEP, GPA2=DIR).
 *   The aux slider value is scaled to steps/sec and written to
 *   g_targetSpeeds[4] via the shared mutex. Core 1's simple_stepper
 *   engine handles the actual pulse generation on Port A.
 *
 * DC MOTORS 3-4 (ON/OFF DIRECTION):
 *   DC Motors 3 and 4 are on MCP23017 U6_2 (0x21) Port A, controlled
 *   via H-bridge direction pins. No speed control (I2C is digital-only).
 *     Aux slider > +5:  Forward  (P=HIGH, N=LOW)
 *     Aux slider < -5:  Reverse  (P=LOW,  N=HIGH)
 *     |Aux| <= 5:       Off      (P=LOW,  N=LOW)
 *
 * THREAD SAFETY:
 *   Motor 5 stepper: uses g_speedMutex (non-blocking) to write to
 *   g_targetSpeeds[4], same pattern as profile_mecanum.
 *   DC motors: write to MCP23017 U6_2 (0x21), which is not accessed by
 *   Core 1 (Core 1 only writes to U6_1 at 0x20). No bus contention.
 *
 * @see project_config.h for ENABLE_MOTOR_5, ENABLE_DC_MOTOR_3/4
 * @see simple_stepper.cpp for Motor 5 pulse generation on Core 1
 * @see mcp23017.h for IN_MOT_3N_BIT, IN_MOT_3P_BIT, etc.
 */

#include "project_config.h"

#if (defined(ENABLE_MOTOR_5) || defined(ENABLE_DC_MOTOR_3) || defined(ENABLE_DC_MOTOR_4))

#include "profile_aux_motors.h"
#include "drivers/mcp23017.h"
#include <Arduino.h>
#include <math.h>

// Deadzone threshold - inputs below this magnitude are treated as "stop"
static const float AUX_DEADZONE = 50.0f;

// =========================================================================
// Motor 5 stepper: shared memory for Core 1 handoff
// =========================================================================
#ifdef ENABLE_MOTOR_5
#include "pico/mutex.h"

extern mutex_t g_speedMutex;
extern volatile float g_targetSpeeds[];
extern volatile bool g_speedsUpdated;
#endif

// =========================================================================
// Apply aux channel inputs to auxiliary motors
// =========================================================================

void profile_aux_motors_apply(const control_command_t *cmd) {

  // --- Motor 5 (stepper via Core 1) ---
#ifdef ENABLE_MOTOR_5
  {
    float m5input = cmd->aux[MOTOR_5_AUX_CHANNEL];
    float m5speed = 0.0f;
    if (fabsf(m5input) > AUX_DEADZONE) {
      m5speed = m5input * (MOTOR_5_MAX_SPEED / 100.0f) * MOTOR_5_DIR_INVERT;
    }
    if (mutex_try_enter(&g_speedMutex, nullptr)) {
      g_targetSpeeds[4] = m5speed;
      g_speedsUpdated = true;
      mutex_exit(&g_speedMutex);
    }
  }
#endif

  // --- DC Motor 3 (on/off direction via MCP23017 U6_2) ---
#ifdef ENABLE_DC_MOTOR_3
  {
    float input = cmd->aux[DC_MOTOR_3_AUX_CHANNEL] * DC_MOTOR_3_DIR_INVERT;
    if (input > AUX_DEADZONE) {
      // Forward: positive HIGH, negative LOW
      mcpDCMotor.setBitA(IN_MOT_3P_BIT, true);
      mcpDCMotor.setBitA(IN_MOT_3N_BIT, false);
    } else if (input < -AUX_DEADZONE) {
      // Reverse: positive LOW, negative HIGH
      mcpDCMotor.setBitA(IN_MOT_3P_BIT, false);
      mcpDCMotor.setBitA(IN_MOT_3N_BIT, true);
    } else {
      // Neutral: both LOW (motor off)
      mcpDCMotor.setBitA(IN_MOT_3P_BIT, false);
      mcpDCMotor.setBitA(IN_MOT_3N_BIT, false);
    }
  }
#endif

  // --- DC Motor 4 (on/off direction via MCP23017 U6_2) ---
#ifdef ENABLE_DC_MOTOR_4
  {
    float input = cmd->aux[DC_MOTOR_4_AUX_CHANNEL] * DC_MOTOR_4_DIR_INVERT;
    if (input > AUX_DEADZONE) {
      mcpDCMotor.setBitA(IN_MOT_4P_BIT, true);
      mcpDCMotor.setBitA(IN_MOT_4N_BIT, false);
    } else if (input < -AUX_DEADZONE) {
      mcpDCMotor.setBitA(IN_MOT_4P_BIT, false);
      mcpDCMotor.setBitA(IN_MOT_4N_BIT, true);
    } else {
      mcpDCMotor.setBitA(IN_MOT_4P_BIT, false);
      mcpDCMotor.setBitA(IN_MOT_4N_BIT, false);
    }
  }
#endif
}

// =========================================================================
// Emergency stop: turn off all auxiliary motors
// =========================================================================

void profile_aux_motors_stop() {
  // Motor 5 stop is handled by simple_stepper_stop_all() on Core 1
  // via the g_emergencyStop flag. No action needed here.

#ifdef ENABLE_DC_MOTOR_3
  mcpDCMotor.setBitA(IN_MOT_3P_BIT, false);
  mcpDCMotor.setBitA(IN_MOT_3N_BIT, false);
#endif
#ifdef ENABLE_DC_MOTOR_4
  mcpDCMotor.setBitA(IN_MOT_4P_BIT, false);
  mcpDCMotor.setBitA(IN_MOT_4N_BIT, false);
#endif
}

#else
// Stub implementations when no auxiliary motors are enabled
#include "profile_aux_motors.h"
void profile_aux_motors_apply(const control_command_t *cmd) {
  (void)cmd;
}
void profile_aux_motors_stop() {}
#endif