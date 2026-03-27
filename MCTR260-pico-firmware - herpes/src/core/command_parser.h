/**
 * @file command_parser.h
 * @brief JSON command parsing: BLE JSON string -> control_command_t struct
 *
 * WHAT THIS MODULE DOES:
 *   When the Flutter app sends a joystick command, it arrives as a JSON
 *   text string over BLE (e.g., {"type":"control","vehicle":"mecanum",...}).
 *   This module parses that text into a C struct (control_command_t) that
 *   the rest of the firmware can work with directly, no string manipulation
 *   needed after this point.
 *
 * WHY A SEPARATE MODULE?
 *   Parsing JSON is a distinct responsibility from BLE communication or
 *   motor control. Keeping it isolated makes the code easier to test and
 *   debug. If commands stop working, you check this module's serial output
 *   to see if parsing succeeded or failed.
 *
 * See command_parser.cpp for the full protocol format and ArduinoJson details.
 * Thread safety: called only on Core 0 (BLE callback context).
 *
 * @see command_packet.h for the struct definition and JSON example
 * @see ble_controller.h which calls command_parse() from its BLE callback
 */

#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include "command_packet.h"

/**
 * @brief Parse a JSON command string into a control_command_t struct.
 *
 * Takes raw JSON text from BLE and fills in the fields of a command struct.
 * On success, the struct is ready to pass to the motion profile. On failure
 * (malformed JSON, missing fields), the struct is left unchanged and an
 * error is printed to serial.
 *
 * @param json  Null-terminated JSON string received from BLE.
 * @param cmd   Pointer to the output struct to fill. Must not be null.
 * @return true if parsing succeeded and @p cmd is valid.
 * @return false if the JSON was malformed or the command type is unknown.
 *
 * @note Also stores the parsed command internally. Other modules can read
 *       it later via command_get_current().
 * @see command_packet.h for example JSON and struct field descriptions
 */
bool command_parse(const char *json, control_command_t *cmd);

/**
 * @brief Get the most recently parsed command.
 *
 * Returns a read-only pointer to the internal copy of the last successfully
 * parsed command. Useful for telemetry (reporting what the robot is doing)
 * and safety checks (verifying the last command type).
 *
 * @return Pointer to the internal command struct. Never null after the
 *         first successful parse. The pointed-to data is valid until the
 *         next call to command_parse() overwrites it.
 *
 * @see command_packet.h for the control_command_t structure
 */
const control_command_t *command_get_current(void);

/**
 * @brief Get the speed multiplier from the last command (0-100).
 *
 * The app has a speed slider that scales all motor outputs. This returns
 * that slider's value from the most recent command.
 *
 * @return Speed multiplier, 0-100. Returns 0 before the first command
 *         is received.
 *
 * @see control_command_t::speed for the field this reads
 */
uint8_t command_get_last_speed(void);

/**
 * @brief Check if the most recent command was a heartbeat.
 *
 * The app sends periodic heartbeat commands ({"type":"heartbeat"}) to
 * keep the safety watchdog fed, even when the joystick isn't moving.
 * Use this to distinguish heartbeats from real control commands.
 *
 * @return true if the last parsed command had type "heartbeat".
 * @return false if it was a control command or no command has been parsed.
 *
 * @see safety_feed() which should be called on every command including
 *      heartbeats
 */
bool command_is_heartbeat(void);

#endif // COMMAND_PARSER_H
