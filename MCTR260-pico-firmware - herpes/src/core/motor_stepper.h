#ifndef MOTOR_STEPPER_H
#define MOTOR_STEPPER_H

#include "motor_base.h"

/**
 * @file motor_stepper.h
 * @brief Stepper motor control class: config structs and trapezoidal profile.
 *
 * WHAT IS A STEPPER MOTOR?
 *   Unlike a DC motor that spins freely, a stepper motor moves in precise
 *   steps (typically 200 per revolution, or 1.8 degrees each). To spin a
 *   stepper, you send it a series of "step" pulses. Each pulse makes the
 *   shaft rotate exactly one step. Faster pulses = faster rotation.
 *
 * HOW STEPPER PINS DIFFER FROM DC:
 *   Steppers on the MechaPico MCB don't use direct Pico GPIO pins. Instead,
 *   step/dir signals route through an MCP23017 I2C GPIO expander (a chip that
 *   gives you extra pins over just 2 wires). So cfg_.pinStep is set to -1
 *   and generateStep() writes to MCP23017 Port B instead of Pico GPIO.
 *
 * WHY TRAPEZOIDAL ACCELERATION?
 *   If you tell a stepper to instantly go from 0 to 4000 steps/sec, it will
 *   stall (miss steps and vibrate instead of spinning). The solution is to
 *   ramp the speed up gradually, like easing onto a gas pedal:
 *
 *     Speed |    /-------\
 *           |   /  cruise  \
 *           |  /             \
 *     0 ----+-/---------------\--> Time
 *           | accel    decel
 *
 *   "Trapezoidal" because the speed vs. time graph looks like a trapezoid.
 *
 * TWO STEPPER IMPLEMENTATIONS EXIST:
 *   1. MotorStepper (this class): full trapezoidal acceleration profile,
 *      OOP design, per-motor I2C writes. Runs on Core 0 for position mode.
 *   2. simple_stepper.cpp: flat C functions, batched I2C writes for all
 *      4 motors simultaneously. Runs on Core 1 for real-time velocity mode.
 *
 * @see simple_stepper.h for the lightweight Core 1 version
 * @see motor_base.h for the abstract interface this class implements
 * @see mcp23017.h for the I2C GPIO expander that drives the motors
 * @see motor_stepper.cpp for implementation details
 */

/**
 * @brief Stepper driver type enumeration.
 *
 * Each driver type has different microstepping capabilities, noise levels,
 * and features. The firmware uses this to configure microstepping pins
 * correctly on the MCP23017.
 *
 * @note TMC2209 and A4988 have DIFFERENT microstepping truth tables for
 *       the same MS1/MS2 pin states. See CONFIGURATION_REFERENCE.md.
 * @see stepperSetMicrostepping() in mcp23017.h for truth tables
 */
enum class StepperDriverType {
  TMC2209, /**< Trinamic silent driver. Very quiet (StealthChop mode),
                up to 256 microsteps internally, UART configurable.
                Best choice for low-noise operation. */
  A4988,   /**< Allegro basic driver. Widely available, affordable,
                up to 1/16 microstepping. Moderate noise. Good for
                budget builds. */
  DRV8825  /**< TI high-current driver. Up to 1/32 microstepping,
                2.5A max. Good middle ground between A4988 and TMC2209. */
};

/**
 * @brief Stepper motor configuration (one per physical motor).
 *
 * Each motor gets its own config struct with driver type, mechanical
 * parameters, and motion limits. These are defined in project_config.h
 * and passed to the MotorStepper constructor during motors_init().
 *
 * @note On the MechaPico MCB, all stepper pins are on the MCP23017 I2C
 *       expander, NOT on Pico GPIO. The legacy pinStep/pinDir/pinEnable
 *       fields are set to -1 and ignored.
 *
 * @see project_config.h where these values are set for your robot
 * @see mcp23017.h for the physical pin mapping on the MCP23017
 */
struct MotorStepperConfig {
  uint8_t index; /**< Motor index (0-4 for M1-M5 on MCP23017). For a 4-motor
                      mecanum: 0=FL, 1=FR, 2=BL, 3=BR. Motor 4 is a spare. */
  bool enabled;  /**< Master on/off switch. Set false to disable this motor
                      completely (useful for testing with fewer motors). */

  StepperDriverType driverType; /**< Which driver IC is used. Affects
                                     microstepping truth table and available
                                     features. @see StepperDriverType */

  // For MCP23017-based control (MechaPico MCB):
  // motorIndex 0-4 maps to M1-M5 on the GPIO expander
  // Direct GPIO pins are NOT used - set to -1
  int8_t pinStep;   /**< Legacy: Step pulse pin. Set to -1 on MechaPico MCB
                         because step pulses go through MCP23017, not Pico
                         GPIO. */
  int8_t pinDir;    /**< Legacy: Direction pin. Set to -1 on MechaPico MCB.
                         Direction is set via MCP23017 Port B bits. */
  int8_t pinEnable; /**< Legacy: Enable pin. Set to -1 on MechaPico MCB.
                         All steppers share one enable signal on MCP23017
                         GPA3 (STPR_ALL_EN). */

  uint16_t stepsPerRev;  /**< Full steps per revolution. Standard stepper
                              motors have 200 steps/rev (1.8 degrees per step).
                              Some high-resolution motors use 400. */
  uint8_t microstepping; /**< Microstep divisor (1, 2, 4, 8, 16, 32).
                              Higher values = smoother motion but lower
                              torque and higher step rates needed for the
                              same RPM. Example: 8 microsteps with a
                              200-step motor gives 1600 effective steps
                              per revolution. */
  int8_t direction;      /**< Direction multiplier: 1 for normal, -1 to
                              reverse. Like DC motors, use this to correct
                              for motors mounted in opposite orientations. */

  float maxSpeed;     /**< Maximum speed in steps per second. The motor
                           will never exceed this rate, even if the joystick
                           is pushed to maximum. Higher values need faster
                           I2C communication. Default: 4000. */
  float acceleration; /**< Acceleration rate in steps per second squared.
                           Controls how quickly the motor ramps up to target
                           speed. Too high = missed steps (motor stalls).
                           Too low = sluggish response. Default: 8000. */
};

/**
 * @brief Motion mode for the stepper motor.
 *
 * Velocity mode: the motor spins continuously at a target speed (like
 * driving). Position mode: the motor moves to a specific step count and
 * stops (like a CNC machine or 3D printer).
 */
enum class StepperMode {
  Velocity, /**< Continuous rotation at whatever speed the joystick commands.
                 This is the normal mode for driving a robot. */
  Position  /**< Move to a target position (step count) and stop. Used for
                 precise positioning. Not yet used in the current firmware
                 but available for future features like homing. */
};

/**
 * @brief Stepper motor with trapezoidal acceleration profile.
 *
 * Implements the MotorBase interface for stepper motors driven through
 * an MCP23017 I2C GPIO expander. Supports both velocity mode (continuous
 * rotation) and position mode (move to a target).
 *
 * @note In the current firmware architecture, simple_stepper.cpp on Core 1
 *       handles the actual real-time pulse generation for driving. This
 *       class is retained for position mode and as a reference
 *       implementation with per-motor acceleration.
 *
 * @see MotorBase for the inherited interface (init, stop, update)
 * @see MotorStepperConfig for the configuration structure
 * @see simple_stepper.h for the Core 1 batch pulse generator
 */
class MotorStepper : public MotorBase {
public:
  /**
   * @brief Construct a stepper motor with the given configuration.
   * @param config Configuration struct with driver type, speed limits, etc.
   *               Typically passed from project_config.h via motors_init().
   */
  explicit MotorStepper(const MotorStepperConfig &config);

  /** @copydoc MotorBase::init()
   *  @details Validates the motor index (must be 0-4 for MCP23017) and
   *           prints initialization info to serial. The actual MCP23017
   *           hardware init is done separately by motors_init().
   */
  bool init() override;

  /** @copydoc MotorBase::stop()
   *  @details Resets target and current speed to zero. The motor stops
   *           generating step pulses. Current position is preserved
   *           (not reset to zero) in case position mode is used later.
   */
  void stop() override;

  /** @copydoc MotorBase::update()
   *  @details Performs two tasks each cycle:
   *           1. Ramps currentSpeed toward targetSpeed using the configured
   *              acceleration rate (trapezoidal profile).
   *           2. Generates step pulses via MCP23017 for each elapsed step
   *              interval. May generate multiple steps per update at high
   *              speeds.
   */
  void update(float dtSec) override;

  /** @copydoc MotorBase::getIndex() */
  uint8_t getIndex() const override;

  /** @copydoc MotorBase::isEnabled() */
  bool isEnabled() const override;

  /** @copydoc MotorBase::getType()
   *  @return Always MotorType::Stepper for this class.
   */
  MotorType getType() const override;

  // =========================================================================
  // Stepper-Specific Methods
  // =========================================================================

  /**
   * @brief Set the target speed (velocity mode).
   *
   * Switches the motor to velocity mode (continuous rotation). The motor
   * won't jump to this speed instantly; the update() function ramps it
   * up or down according to the configured acceleration.
   *
   * @param stepsPerSec Target speed in steps per second. Positive = forward,
   *                    negative = reverse. Will be clamped to maxSpeed.
   *                    The direction correction (cfg_.direction) is applied
   *                    internally.
   *
   * @see profile_mecanum_apply() which calculates and sets these speeds
   */
  void setTargetSpeed(float stepsPerSec);

  /**
   * @brief Move to an absolute step position (position mode).
   *
   * Switches the motor to position mode. The motor will accelerate, cruise,
   * and decelerate to reach the target position, then stop.
   *
   * @param targetSteps Target position in full steps (not microsteps).
   *                    Internally multiplied by the microstepping factor.
   *
   * @note Not currently used in the driving firmware, but available for
   *       future features like automated parking or arm positioning.
   */
  void moveTo(int32_t targetSteps);

  /**
   * @brief Move a relative number of steps from the current position.
   *
   * Convenience function that calls moveTo() with (currentPosition + steps).
   *
   * @param steps Number of full steps to move. Positive = forward,
   *              negative = reverse.
   */
  void moveRelative(int32_t steps);

  /**
   * @brief Get the current position in full steps.
   *
   * @return Current position divided by the microstepping factor.
   *         Positive = forward from home, negative = backward.
   *
   * @see setHome() to reset this to zero
   */
  int32_t getPosition() const;

  /**
   * @brief Get the current actual speed in steps per second.
   *
   * This is the ramped speed (after acceleration), not the target.
   * During acceleration, this will be between 0 and the target speed.
   *
   * @return Current speed divided by the microstepping factor.
   *         Positive = forward, negative = reverse.
   */
  float getCurrentSpeed() const;

  /**
   * @brief Check if the motor is currently in motion.
   *
   * @return true if the motor's current speed is above 1 step/sec
   *         (i.e., it's actively generating step pulses).
   * @return false if the motor is stopped or below the deadzone threshold.
   */
  bool isMoving() const;

  /**
   * @brief Set the current position as home (zero point).
   *
   * Resets both currentPosition and targetPosition to zero. Use this
   * after a homing sequence (e.g., bumping into a limit switch).
   */
  void setHome();

  /**
   * @brief Update timing and acceleration for batched step generation.
   *
   * Used by motor_manager when generating batched step pulses for multiple
   * motors simultaneously. Returns whether this motor needs a step pulse
   * this cycle.
   *
   * @param dtSec Delta time in seconds since last call.
   * @return true if a step pulse is needed this cycle.
   * @return false if the motor is stopped or it's not time for a step yet.
   *
   * @see stepperPulseBatchPortB() in mcp23017.h for batched I2C writes
   */
  bool updateTiming(float dtSec);

  /**
   * @brief Get the MCP23017 Port B step bit mask for this motor.
   *
   * Returns the bitmask used to set the STEP pin in a batched Port B write.
   * For example, motor 0 (M1) returns 0x02 (GPB1 = STPR_M1_STEP).
   *
   * @return Bit mask for this motor's STEP signal on MCP23017 Port B.
   *
   * @see mcp23017.h for the full pin mapping
   */
  uint8_t getStepBit() const;

  /**
   * @brief Get the MCP23017 Port B direction bit mask for this motor.
   *
   * Returns the bitmask used to set the DIR pin in a batched Port B write.
   * For example, motor 0 (M1) returns 0x01 (GPB0 = STPR_M1_DIR).
   *
   * @return Bit mask for this motor's DIR signal on MCP23017 Port B.
   *
   * @see mcp23017.h for the full pin mapping
   */
  uint8_t getDirBit() const;

  /**
   * @brief Get the desired direction for this motor's next step.
   *
   * @return true if the motor should step forward.
   * @return false if the motor should step in reverse.
   */
  bool getDesiredDirection() const;

  /**
   * @brief Notify the motor that a step was generated externally.
   *
   * Called after a batched step pulse to update the motor's internal
   * position counter and timing state. Without this call, the position
   * tracker would drift out of sync with the actual motor position.
   */
  void acknowledgeStep();

private:
  MotorStepperConfig cfg_; /**< Motor configuration (pins, limits, etc.) */

  // Motion state
  StepperMode mode_;         /**< Current mode: Velocity or Position */
  int32_t currentPosition_;  /**< Current position in microsteps */
  int32_t targetPosition_;   /**< Target position for position mode */
  float targetSpeed_;        /**< Target speed for velocity mode */
  float currentSpeed_;       /**< Current speed with ramp applied */

  // Step timing
  unsigned long lastStepTime_; /**< Timestamp of the last step (microseconds) */
  unsigned long stepInterval_; /**< Current interval between steps (us). Shorter
                                    interval = faster motor speed. */
  bool lastDirection_;         /**< Cached direction to avoid redundant I2C
                                    writes. Only sends a direction change to
                                    MCP23017 when it actually changes. */

  /**
   * @brief Compute the step interval from the current speed.
   * @details Converts speed (steps/sec) to interval (microseconds/step)
   *          using: interval = 1,000,000 / |speed|. Enforces a minimum
   *          interval of 10us to prevent impossibly fast stepping.
   */
  void calculateStepInterval();

  /**
   * @brief Generate a single step pulse via MCP23017.
   * @details Sets the direction bit (if changed), then toggles the STEP
   *          pin high then low. Each step pulse requires 2 I2C writes.
   *          Also updates the position counter.
   */
  void generateStep();
};

#endif // MOTOR_STEPPER_H
