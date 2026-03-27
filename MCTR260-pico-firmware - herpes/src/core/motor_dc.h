/**
 * @file motor_dc.h
 * @brief DC motor control class: config structs and PWM interface.
 *
 * WHAT IS A DC MOTOR?
 *   A DC motor spins when you apply voltage. Reverse the voltage polarity
 *   and it spins the other way. The speed is controlled by PWM (Pulse Width
 *   Modulation): rapidly switching the voltage on and off. A higher duty
 *   cycle (more "on" time) means faster spinning.
 *
 * WHAT IS AN H-BRIDGE?
 *   An H-bridge is a circuit that lets you reverse the voltage across the
 *   motor using digital signals. The Pico can't drive a motor directly
 *   (its GPIO pins provide only 3.3V at a few milliamps). Instead, the
 *   Pico sends PWM signals to the H-bridge IC, which switches the motor's
 *   full supply voltage.
 *
 * SUPPORTED DRIVERS:
 *   - DRV8871: 1 motor per chip, 2 PWM pins. Simple and reliable.
 *   - DRV8833: 2 motors per chip, 2 PWM pins per motor. Compact.
 *   - L298N:   2 motors per chip, 3 pins per motor (enable + 2 direction).
 *              Older design, runs hot, but very common and easy to find.
 *
 * See motor_dc.cpp for H-bridge driver operation details.
 *
 * WIRING:
 *   DRV8871/DRV8833 need pinA + pinB only.
 *   L298N needs pinA + pinB + pinEnable.
 *
 * DIRECTION CORRECTION:
 *   Set cfg_.direction to -1 to reverse a physically flipped motor
 *   (e.g., left-side motors on a robot are mounted backwards).
 *
 * @see motor_base.h for the abstract interface this class implements
 * @see motor_manager.h which creates and manages MotorDC instances
 * @see project_config.h for GPIO pin assignments
 */

#ifndef MOTOR_DC_H
#define MOTOR_DC_H

#include "motor_base.h"

/**
 * @brief DC motor driver type enumeration.
 *
 * Each driver type has a different pin configuration and control method.
 * Choose the one that matches the driver IC on your robot.
 *
 * @see MotorDCConfig::driverType
 */
enum class DCDriverType {
  DRV8871, /**< Single H-bridge, 2 PWM pins (IN1, IN2). No separate enable. */
  DRV8833, /**< Dual H-bridge, 2 PWM pins per channel. Same control as DRV8871
                but fits 2 motors per chip. */
  L298N    /**< Dual H-bridge with separate enable pin. 3 pins per motor:
                enable (PWM for speed) + IN1/IN2 (direction). Older design,
                commonly found in hobby kits. */
};

/**
 * @brief DC motor configuration (one per physical motor).
 *
 * Each motor gets its own config struct with pin numbers, driver type,
 * and direction. These are defined in project_config.h and passed to
 * the MotorDC constructor during motors_init().
 *
 * @see project_config.h where these values are set for your robot
 */
struct MotorDCConfig {
  uint8_t index; /**< Motor index (0-3 for a 4-wheel mecanum robot).
                      0=Front-Left, 1=Front-Right, 2=Back-Left, 3=Back-Right */
  bool enabled;  /**< Master on/off switch. Set false to disable this motor
                      completely (useful for testing with fewer motors). */

  DCDriverType driverType; /**< Which driver IC is used. Determines how the
                                GPIO pins are controlled. @see DCDriverType */
  int8_t pinA;      /**< First GPIO pin. For DRV8871/DRV8833: PWM output to IN1.
                         For L298N: direction pin IN1. */
  int8_t pinB;      /**< Second GPIO pin. For DRV8871/DRV8833: PWM output to IN2.
                         For L298N: direction pin IN2. */
  int8_t pinEnable; /**< Enable/speed GPIO pin (L298N only). PWM output that
                         controls motor speed. Set to -1 for DRV8871/DRV8833
                         (they don't have a separate enable). */

  int8_t direction; /**< Direction multiplier: 1 for normal, -1 to reverse.
                         Motors on opposite sides of a robot spin in opposite
                         physical directions for the same "forward" command.
                         Use -1 to correct this in software instead of
                         rewiring. */
};

/**
 * @brief DC motor with open-loop PWM control.
 *
 * Implements the MotorBase interface for DC motors driven by an H-bridge.
 * The motor speed is set as a PWM value (-255 to +255), where the sign
 * determines direction and the magnitude determines speed.
 *
 * "Open-loop" means there is no feedback (no encoder). The firmware sets
 * a PWM value and trusts that the motor responds accordingly. For closed-loop
 * control with encoders, see the ESP32 firmware's PID implementation.
 *
 * @see MotorBase for the inherited interface (init, stop, update)
 * @see MotorDCConfig for the configuration structure
 */
class MotorDC : public MotorBase {
public:
  /**
   * @brief Construct a DC motor with the given configuration.
   * @param config Configuration struct with pin numbers and driver type.
   *               Typically passed from project_config.h via motors_init().
   */
  explicit MotorDC(const MotorDCConfig &config);

  /** @copydoc MotorBase::init()
   *  @details Configures GPIO pins as outputs, sets PWM frequency to 20kHz
   *           (above human hearing to avoid motor whine), and ensures the
   *           motor starts in a stopped state.
   */
  bool init() override;

  /** @copydoc MotorBase::stop() */
  void stop() override;

  /** @copydoc MotorBase::update()
   *  @details For DC motors, this applies the target PWM value immediately
   *           (open-loop, no ramping). Future versions could add PWM ramping
   *           here for smoother acceleration.
   */
  void update(float dtSec) override;

  /** @copydoc MotorBase::getIndex() */
  uint8_t getIndex() const override;

  /** @copydoc MotorBase::isEnabled() */
  bool isEnabled() const override;

  /** @copydoc MotorBase::getType()
   *  @return Always MotorType::DC for this class.
   */
  MotorType getType() const override;

  /**
   * @brief Set the target PWM value (speed and direction).
   *
   * Positive values spin the motor forward, negative values spin it in
   * reverse. The magnitude controls speed: 0 = stopped, 255 = full speed.
   *
   * @param pwm PWM value from -255 (full reverse) to +255 (full forward).
   *            Values outside this range are clamped. The direction
   *            correction (cfg_.direction) is applied internally.
   *
   * @note The actual PWM is applied on the next update() call, not
   *       immediately. In practice, update() runs at 50Hz, so the
   *       delay is at most 20ms.
   * @see motor_set_pwm() in motor_manager.h for a convenient wrapper
   */
  void setTarget(int16_t pwm);

  /**
   * @brief Get the current target PWM value.
   *
   * @return The PWM value set by setTarget(), after direction correction
   *         and clamping. Range: -255 to +255.
   */
  int16_t getTarget() const;

private:
  MotorDCConfig cfg_;   /**< Configuration (pins, driver type, direction). */
  int16_t targetPWM_;   /**< Desired PWM value (from setTarget). */
  int16_t currentPWM_;  /**< Currently applied PWM value. */

  /**
   * @brief Write the PWM signal to the hardware pins.
   *
   * Handles the pin logic differences between DRV8871/DRV8833 (2-pin)
   * and L298N (3-pin) driver types.
   *
   * @param pwm Signed PWM value. Sign determines direction, magnitude
   *            determines duty cycle.
   */
  void applyPWM(int16_t pwm);
};

#endif // MOTOR_DC_H
