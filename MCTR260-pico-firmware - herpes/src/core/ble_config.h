/**
 * @file ble_config.h
 * @brief BLE UUIDs and constants for Pico W
 *
 * UUID COORDINATION (three codebases must agree!):
 *   These UUIDs are shared between three codebases:
 *     1. ESP32 firmware  -> esp32_firmware/src/ble_config.h
 *     2. Pico firmware   -> this file
 *     3. Flutter app     -> lib/services/ble_service.dart
 *
 *   If you change a UUID here, you MUST change it in all three places.
 *   A UUID mismatch will cause the app to connect but fail to discover
 *   characteristics; commands won't be received and telemetry won't be sent.
 *
 * WHAT IS A UUID?
 *   A UUID (Universally Unique Identifier) is a 128-bit label that
 *   identifies a BLE service or characteristic. Think of it like a street
 *   address: the app needs to know the exact address to find the right
 *   "mailbox" on your robot to send commands to.
 */

#ifndef BLE_CONFIG_H
#define BLE_CONFIG_H

/**
 * @defgroup ble_uuids BLE Service and Characteristic UUIDs
 * @brief UUIDs that identify the BLE communication channels.
 *
 * The app uses these UUIDs to find your robot's control "mailbox" (where
 * it sends joystick commands) and telemetry "mailbox" (where it reads
 * battery level and motor speed data back).
 *
 * @warning Changing any UUID here requires matching changes in the ESP32
 *          firmware AND the Flutter app, or the connection will silently fail.
 * @{
 */

/** @brief Primary BLE service UUID for robot control.
 *
 *  This is the top-level service that groups all robot communication.
 *  The app scans for this UUID to identify compatible robots.
 *  Must match ESP32 firmware (esp32_firmware/src/ble_config.h)
 *  and Flutter app (lib/services/ble_service.dart).
 */
#define CONTROL_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"

/** @brief Control commands characteristic UUID (app writes JSON here).
 *
 *  The Flutter app writes joystick/dial commands as JSON strings to this
 *  characteristic. The firmware receives them via the gattWriteCallback
 *  in ble_controller.cpp.
 *
 *  @see command_parser.h for how the JSON is parsed into a struct
 */
#define CONTROL_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

/** @brief Telemetry/status characteristic UUID (firmware sends
 *         notifications here).
 *
 *  The firmware publishes status data (battery voltage, motor speeds,
 *  connection quality) to this characteristic. The app subscribes to
 *  notifications to receive updates without polling.
 *
 *  @see ble_send_telemetry() for how data is published
 */
#define TELEMETRY_CHAR_UUID "beb5483f-36e1-4688-b7f5-ea07361b26a8"

/** @} */ // end of ble_uuids group

/**
 * @defgroup ble_params BLE Connection Parameters
 * @brief Tuning values that control BLE communication speed and reliability.
 * @{
 */

/** @brief Maximum BLE attribute value length in bytes (MTU minus 3).
 *
 *  This limits how large a single BLE read or write can be. 512 bytes is
 *  generous; most JSON commands are under 200 bytes. If you see truncated
 *  commands in the serial log, this value may need to increase.
 */
#define BLE_ATT_MAX_VALUE_LEN 512

/** @brief Minimum BLE connection interval in 1.25ms units.
 *
 *  6 x 1.25ms = 7.5ms. This is the fastest the Pico and phone will
 *  exchange data. Lower = more responsive controls but higher battery drain.
 *
 *  @note The phone's BLE stack may override this to a longer interval.
 */
#define BLE_CONN_INTERVAL_MIN 6

/** @brief Maximum BLE connection interval in 1.25ms units.
 *
 *  24 x 1.25ms = 30ms. If the connection is idle (no joystick movement),
 *  the interval may stretch up to this value to save battery. The app sends
 *  heartbeats to keep the connection alive during idle periods.
 */
#define BLE_CONN_INTERVAL_MAX 24

/** @} */ // end of ble_params group

#endif // BLE_CONFIG_H
