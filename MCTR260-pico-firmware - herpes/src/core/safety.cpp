/**
 * @file safety.cpp
 * @brief Motor safety watchdog: automatic stop on lost connection
 *
 * THE PROBLEM:
 *   If the phone disconnects (BLE drop, app crash, battery dies, user walks
 *   out of range), the last command stays active indefinitely. With a robot
 *   moving at full speed, this is dangerous.
 *
 * THE SOLUTION (watchdog pattern):
 *   1. The Flutter app sends heartbeat packets every 1000ms
 *   2. Each received command calls safety_feed() to reset the timer
 *   3. safety_check_timeout() is called every loop iteration
 *   4. If no command arrives for SAFETY_TIMEOUT_MS (2000ms),
 *      all motors are stopped immediately
 *
 * RELATIONSHIP TO BLE:
 *   The watchdog is INDEPENDENT of BLE connection state. Even if BLE reports
 *   "connected" (which can lag by seconds on iOS), the watchdog will trigger
 *   if data stops flowing. This is more reliable than connection callbacks.
 */

#include "safety.h"
#include "motor_manager.h"
#include "project_config.h"
#include <Arduino.h>

// =============================================================================
// PRIVATE STATE
// =============================================================================

static unsigned long s_lastFeedTime = 0;
static bool s_stopTriggered = false;

// =============================================================================
// PUBLIC API
// =============================================================================

/**
 * @details Sets the feed time to "now" so the watchdog doesn't trigger
 * immediately on boot (before any BLE connection exists). The timeout
 * clock doesn't start meaningfully until the first real command arrives.
 */
void safety_init() {
  s_lastFeedTime = millis();
  s_stopTriggered = false;
  Serial.printf("[Safety] Watchdog initialized (timeout: %dms)\n",
                SAFETY_TIMEOUT_MS);
}

/**
 * @details Resets the countdown timer and clears the stop flag. Clearing
 * s_stopTriggered allows the motors to move again after a timeout recovery
 * (i.e., the app reconnects and starts sending commands again).
 */
void safety_feed() {
  s_lastFeedTime = millis();
  s_stopTriggered = false;
}

/**
 * @details Uses unsigned subtraction (now - lastFeed), which handles
 * millis() rollover correctly at ~49 days. The s_stopTriggered flag
 * ensures motors_stop_all() is called only ONCE per timeout event
 * (not every loop iteration), preventing repeated serial log spam
 * and redundant I2C stop commands.
 */
bool safety_check_timeout() {
  unsigned long now = millis();
  unsigned long elapsed = now - s_lastFeedTime;

  if (elapsed >= SAFETY_TIMEOUT_MS) {
    if (!s_stopTriggered) {
      Serial.printf("[Safety] TIMEOUT at %lu ms - Stopping all motors!\n", now);
      motors_stop_all();
      s_stopTriggered = true; // One-shot: don't call stop again until re-fed
    }
    return true;
  }

  return false;
}

/** @details Simple wrapper for telemetry. Uses the same unsigned
 *  subtraction as safety_check_timeout() for rollover safety. */
unsigned long safety_get_idle_time() { return millis() - s_lastFeedTime; }
