/**
 * @file safety.h
 * @brief Motor safety watchdog: automatically stops motors if the app
 *        disconnects or crashes.
 *
 * WHY THIS MATTERS:
 *   Imagine your phone runs out of battery while driving the robot. Without
 *   a safety system, the motors would keep running at whatever speed they
 *   were last commanded, and your robot drives off a table or into a wall.
 *   The watchdog prevents this: if no command arrives within a timeout
 *   period, it triggers an emergency stop.
 *
 * HOW IT WORKS:
 *   - Every time a BLE command arrives (including heartbeats), call
 *     safety_feed(). This resets the watchdog timer.
 *   - In your main loop, call safety_check_timeout(). If enough time
 *     has passed since the last feed, it returns true, meaning "stop
 *     the motors NOW."
 *   - Think of it like a dead man's switch on a train: the driver must
 *     keep pressing a button every few seconds, or the train stops.
 *
 * USAGE:
 *   safety_init()            - call once at startup
 *   safety_feed()            - call on EVERY received BLE command
 *   safety_check_timeout()   - call in loop(); true = stop motors
 *
 * @see SAFETY_TIMEOUT_MS in project_config.h (default: 2000ms)
 * @see ble_controller.h which calls safety_feed() on each received command
 * @see motors_stop_all() which is called when the watchdog triggers
 */

#ifndef SAFETY_H
#define SAFETY_H

/**
 * @brief Initialize the safety watchdog.
 *
 * Sets the internal timer to the current time so the watchdog doesn't
 * trigger immediately on boot (before any BLE connection exists).
 * Call once in setup() before starting BLE.
 *
 * @note Must be called before safety_check_timeout() or it will
 *       immediately return true (timeout) on the first check.
 */
void safety_init();

/**
 * @brief Feed the watchdog timer (reset the countdown).
 *
 * Call this every time ANY BLE data is received, including heartbeats.
 * This tells the watchdog "the app is still alive and connected."
 * If you forget to call this, the watchdog will trigger a motor stop
 * after SAFETY_TIMEOUT_MS milliseconds.
 *
 * @warning If this function is not called regularly, motors WILL be
 *          stopped automatically. This is a safety feature, not a bug.
 * @see safety_check_timeout() which checks if the timer has expired
 * @see SAFETY_TIMEOUT_MS in project_config.h for the timeout duration
 */
void safety_feed();

/**
 * @brief Check if the safety timeout has expired.
 *
 * Call this in your main loop. If it returns true, immediately stop all
 * motors. The firmware does this automatically in main.cpp's loop().
 *
 * @return true if no command has been received within SAFETY_TIMEOUT_MS.
 *         This means the app is probably disconnected or crashed, and
 *         motors should be stopped immediately.
 * @return false if a command was received recently (robot is being
 *         actively controlled).
 *
 * @note This function only CHECKS the timeout. It does NOT stop the
 *       motors itself. The caller (main.cpp) is responsible for calling
 *       motors_stop_all() when this returns true.
 * @see safety_feed() which resets the timer
 */
bool safety_check_timeout();

/**
 * @brief Get how long it has been since the last command (milliseconds).
 *
 * Useful for telemetry or debugging. If this value is climbing toward
 * SAFETY_TIMEOUT_MS, the connection is about to be declared lost.
 *
 * @return Milliseconds since the last safety_feed() call.
 *
 * @see SAFETY_TIMEOUT_MS in project_config.h for the threshold
 */
unsigned long safety_get_idle_time();

#endif // SAFETY_H
