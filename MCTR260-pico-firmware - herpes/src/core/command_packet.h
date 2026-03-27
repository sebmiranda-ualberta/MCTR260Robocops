/**
 * @file command_packet.h
 * @brief Control command structure: the data contract between Flutter and
 * firmware
 *
 * This struct mirrors the JSON format sent by the Flutter app. Both the ESP32
 * and Pico firmwares use the same struct so either can be a drop-in
 * replacement.
 *
 * HOW COMMANDS FLOW:
 *   1. You move the joystick in the Flutter app.
 *   2. The app sends a JSON string over BLE (see example below).
 *   3. command_parser.cpp converts that JSON into this struct.
 *   4. The motion profile (e.g., profile_mecanum.cpp) reads the struct
 *      and calculates individual wheel speeds.
 *
 * VEHICLE TYPE -> CONTROL MAPPING (Flutter app side):
 *   mecanum:     left = dial (rotation omega)    right = joystick (vx, vy)
 *   fourwheel:   left = dial (steering)      right = slider (throttle)
 *   tracked:     left = slider (L track)     right = slider (R track)
 *   dual:        left = joystick             right = joystick
 *
 * @see command_parser.h for the parsing functions
 * @see profile_mecanum.h for how commands become motor speeds
 */

#ifndef COMMAND_PACKET_H
#define COMMAND_PACKET_H

#include <cstring>
#include <stdint.h>

// =============================================================================
// COMMAND PACKET STRUCTURE
// =============================================================================

/** @brief Maximum number of auxiliary (extra) control channels.
 *
 *  Aux channels are spare sliders in the app that you can map to custom
 *  functions (e.g., a gripper servo, LED brightness, camera tilt).
 */
#define AUX_CHANNEL_COUNT 6

/** @brief Maximum number of toggle switches.
 *
 *  Toggles are on/off switches in the app for features like headlights,
 *  horn, or mode selection.
 */
#define TOGGLE_COUNT 6

/**
 * @brief Joystick or dial input data from one side of the app controller.
 *
 * The app has a left control and a right control. Each can be configured
 * as a joystick (2-axis: x and y), a dial (1-axis rotation), or a slider
 * (1-axis linear). This struct holds whichever type is active.
 *
 * @see control_command_t which contains two of these (left and right)
 */
typedef struct {
  bool isJoystick;  /**< true = this control is a 2-axis joystick,
                         false = it's a 1-axis dial or slider */
  float x;          /**< Horizontal axis: -100 (full left) to +100 (full right).
                         Only meaningful when isJoystick is true. */
  float y;          /**< Vertical axis: -100 (full back) to +100 (full forward).
                         Only meaningful when isJoystick is true. */
  float value;      /**< Single-axis value: -100 to +100. Used for dials
                         (rotation) and sliders (linear). Only meaningful when
                         isJoystick is false. */
} joystick_input_t;

/**
 * @brief Complete control command from the Flutter app.
 *
 * This is the main data structure that flows through the entire firmware.
 * It is populated by command_parser.cpp from JSON and consumed by the
 * motion profile to calculate motor speeds.
 *
 * Example JSON from the app:
 * @code{.json}
 * {
 *   "type": "control",
 *   "vehicle": "mecanum",
 *   "left": {"control": "joystick", "x": 0.0, "y": 50.0},
 *   "right": {"control": "dial", "value": 0.0},
 *   "speed": 80,
 *   "aux": [0.0, 0.0, 0.0, 0.0, 0.0, 0.0],
 *   "toggles": [false, false, false, false, false, false]
 * }
 * @endcode
 *
 * @see command_parse() which fills this struct from JSON
 * @see profile_mecanum_apply() which reads this struct
 */
typedef struct {
  char type[16];                /**< Command type string: "control" for motor
                                     commands, "heartbeat" for keep-alive pings
                                     that tell the safety watchdog the app is
                                     still connected. */
  char vehicle[16];             /**< Vehicle configuration string. Must be one
                                     of: "mecanum", "fourwheel", "tracked", or
                                     "dual". This determines how the left/right
                                     inputs are interpreted. */
  joystick_input_t left;        /**< Left control input from the app.
                                     @see joystick_input_t for field details. */
  joystick_input_t right;       /**< Right control input from the app.
                                     @see joystick_input_t for field details. */
  uint8_t speed;                /**< Speed multiplier from the app's speed
                                     slider, 0-100%. Applied as a scaling factor
                                     to all motor outputs. 100 = full speed. */
  float aux[AUX_CHANNEL_COUNT]; /**< Auxiliary control channels (6 spare
                                     sliders). Range: -100 to +100 each. Map
                                     these to custom functions in your code. */
  bool toggles[TOGGLE_COUNT];   /**< Toggle switch states (6 on/off switches).
                                     Use for features like headlights or mode
                                     selection. */
} control_command_t;

// =============================================================================
// DEFAULT VALUES
// =============================================================================

/**
 * @brief Initialize a command to safe defaults (all zeros, stopped).
 *
 * Call this before first use to ensure no garbage values cause unexpected
 * motor movement. Sets type to "none" and vehicle to "mecanum".
 *
 * @param cmd Pointer to the command struct to initialize. Must not be null.
 *
 * @note This is called automatically by command_parser.cpp on first parse.
 *       You typically don't need to call it yourself.
 */
static inline void command_init(control_command_t *cmd) {
  memset(cmd, 0, sizeof(control_command_t));
  strcpy(cmd->type, "none");
  strcpy(cmd->vehicle, "mecanum");
}

#endif // COMMAND_PACKET_H
