/**
 * @file ble_controller.h
 * @brief BLE GATT server public API: init, callbacks, and telemetry
 *
 * WHAT IS BLE?
 *   Bluetooth Low Energy (BLE) is how your phone talks to the robot wirelessly.
 *   Unlike classic Bluetooth (used for audio), BLE is designed for small, fast
 *   data packets, perfect for sending joystick commands 50 times per second.
 *
 * HOW THIS MODULE WORKS:
 *   1. ble_init() sets up the Pico W as a BLE "peripheral" (server).
 *   2. The Flutter app connects as a "central" (client).
 *   3. When the app writes joystick data, your onCommand callback fires.
 *   4. When the app connects or disconnects, your onConnection callback fires.
 *   5. You can send data back to the app via ble_send_telemetry().
 *
 * THREAD SAFETY:
 *   All functions here run on Core 0. BLE callbacks fire from BTstack's
 *   event loop (also Core 0), so no cross-core synchronization needed.
 *
 * INIT ORDER: ble_init() must be called LAST in setup() because BTstack
 * takes over Core 0's scheduling. Anything after ble_init() won't run
 * until the first BTstack idle callback.
 *
 * @see ble_config.h for the UUIDs that must match the Flutter app
 * @see command_parser.h for how received JSON is turned into motor commands
 * @see safety.h for the watchdog that stops motors if BLE disconnects
 */

#ifndef BLE_CONTROLLER_H
#define BLE_CONTROLLER_H

#include <stdint.h>

// =============================================================================
// CALLBACK TYPES
// =============================================================================

/**
 * @brief Callback type for when a control command arrives over BLE.
 *
 * You provide a function matching this signature to ble_init(). It will be
 * called every time the app sends a JSON command (joystick, heartbeat, etc.)
 *
 * @param jsonData  Null-terminated JSON string from the app.
 * @param length    Length of the JSON string in bytes (not including null).
 *
 * @note This callback fires on Core 0 from BTstack's event loop. Keep
 *       processing fast (under 5ms) to avoid BLE timeouts.
 */
typedef void (*CommandReceivedCallback)(const char *jsonData, uint16_t length);

/**
 * @brief Callback type for when BLE connection state changes.
 *
 * Fires when a phone connects to or disconnects from the robot.
 *
 * @param connected  true if a device just connected, false if disconnected.
 *
 * @note On disconnect, you should stop all motors immediately. The firmware
 *       handles this automatically via the safety module.
 */
typedef void (*ConnectionStateCallback)(bool connected);

// =============================================================================
// PUBLIC API
// =============================================================================

/**
 * @brief Initialize the BLE GATT server and start advertising.
 *
 * This is the one-time setup that makes your robot visible to the Flutter app.
 * After calling this, the robot will appear in the app's "Scan" list with
 * the name set in DEVICE_NAME (project_config.h).
 *
 * @param onCommand     Your function to handle incoming JSON commands.
 *                      Called every time the app sends joystick data.
 * @param onConnection  Your function to handle connect/disconnect events.
 *                      Called when a phone pairs or drops the connection.
 *
 * @warning Must be called LAST in setup(). BTstack takes over Core 0's
 *          main loop after this call. Any code placed after ble_init()
 *          in setup() will not execute until BTstack yields.
 * @note The BLE MAC address is derived from DEVICE_NAME + BLE_PASSKEY.
 *       Changing either setting makes the robot appear as a "new device"
 *       to the phone, requiring re-pairing.
 *
 * @see DEVICE_NAME and BLE_PASSKEY in project_config.h
 * @see ble_config.h for the UUIDs registered during init
 */
void ble_init(CommandReceivedCallback onCommand,
              ConnectionStateCallback onConnection);

/**
 * @brief Process pending BLE events (call from main loop).
 *
 * This pumps BTstack's internal event queue. Without this call in loop(),
 * BLE communication stops entirely. In practice, main.cpp calls this
 * continuously; you shouldn't need to call it yourself.
 *
 * @note Internally calls BTstack.loop(). Executes quickly (~10us) when
 *       no events are pending.
 */
void ble_update(void);

/**
 * @brief Check if a BLE client (phone) is currently connected.
 *
 * @return true if a phone is connected and the BLE link is active.
 * @return false if no phone is connected or the connection dropped.
 *
 * @note A return of true does NOT mean the app is sending commands.
 *       Use ble_first_write_received() to confirm the app completed
 *       its handshake and is actively controlling the robot.
 * @see ble_first_write_received() for full handshake confirmation
 */
bool ble_is_connected(void);

/**
 * @brief Check if the app has sent its first command (handshake complete).
 *
 * On iOS, a BLE connection can be established at the link layer before the
 * app is ready. The user might connect, see the PIN prompt, then cancel.
 * This function returns true only after the app actually writes data to the
 * control characteristic, confirming real two-way communication.
 *
 * @return true if at least one command or heartbeat has been received.
 * @return false if connected but no data has arrived yet.
 *
 * @see ble_is_connected() for link-layer connection status
 */
bool ble_first_write_received(void);

/**
 * @brief Send telemetry data back to the app.
 *
 * Publishes a JSON string to the telemetry BLE characteristic. The app
 * receives it as a notification (if subscribed). Use this to report
 * battery voltage, motor speeds, sensor readings, etc.
 *
 * @param jsonData  Null-terminated JSON string to send (max 255 bytes).
 * @return true if the data was queued for sending.
 * @return false if no client is connected (data is silently dropped).
 *
 * @note Data is not sent immediately. It is buffered until BTstack's next
 *       event cycle. If you call this faster than the BLE connection
 *       interval, only the latest value is sent.
 * @see TELEMETRY_INTERVAL_MS in project_config.h for send rate
 */
bool ble_send_telemetry(const char *jsonData);

/**
 * @brief Get the timestamp of the last received command (in microseconds).
 *
 * Returns the value of micros() at the moment the last BLE write was
 * received (any write: control command, heartbeat, or unknown). The
 * safety module uses this to detect connection loss: if too much time
 * passes without a command, it triggers an emergency stop.
 *
 * @return Timestamp in microseconds (from the micros() clock).
 *
 * @note The return value wraps around every ~70 minutes (32-bit overflow).
 *       The safety module handles this correctly using unsigned subtraction.
 * @see safety_check_timeout() which compares this to the current time
 * @see SAFETY_TIMEOUT_MS in project_config.h for the timeout threshold
 */
unsigned long ble_get_last_command_time(void);

#endif // BLE_CONTROLLER_H
