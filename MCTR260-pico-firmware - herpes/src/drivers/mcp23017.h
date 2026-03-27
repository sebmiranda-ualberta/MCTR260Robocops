/**
 * @file mcp23017.h
 * @brief MCP23017 16-bit I2C GPIO Expander Driver
 *
 * WHY AN I2C GPIO EXPANDER?
 *   The Raspberry Pi Pico has 26 GPIO pins, but controlling 4 stepper motors
 *   (step + dir each = 8 pins), plus enable, microstepping, and spread mode
 *   (3 more pins), plus I2C itself (2 pins) would consume 13 pins, over half
 *   of the Pico. The MCP23017 provides 16 additional GPIOs over just 2 I2C
 *   wires (SDA and SCL).
 *
 * WHAT IS I2C?
 *   I2C (Inter-Integrated Circuit) is a communication bus that lets multiple
 *   chips share just 2 wires: SDA (data) and SCL (clock). Each chip has a
 *   unique 7-bit address (like a phone number). The Pico acts as the "master"
 *   and the MCP23017 chips are "slaves" that respond to their address.
 *
 * MechaPico MCB Layout (custom PCB):
 *   The board has TWO MCP23017 chips on the same I2C bus:
 *
 *   ┌─────────────────────────────────────────────────────┐
 *   │  U6_1 (MCP23017 @ address 0x20): STEPPER CONTROL   │
 *   │    Port B: M1-M4 step + direction (8 pins total)    │
 *   │    Port A: M5 step/dir, enable, MS1, MS2, spread   │
 *   ├─────────────────────────────────────────────────────┤
 *   │  U6_2 (MCP23017 @ address 0x21): DC MOTORS & LEDs  │
 *   │    Port A: 2x DC motor H-bridge + 4x LED bar       │
 *   │    Port B: 8x general purpose 5V I/O                │
 *   └─────────────────────────────────────────────────────┘
 *
 * PERFORMANCE NOTE:
 *   Each I2C write takes ~25us at 400kHz. A single stepperPulse() needs
 *   2 writes (high + low) = ~50us. For 4 motors individually = ~200us.
 *   The batch functions (stepperPulseBatchPortB) do all 4 motors in 2 writes
 *   = ~50us total, a 4x improvement. This is why simple_stepper.cpp uses
 *   setPortB() directly instead of stepperPulse() per motor.
 *
 * @see simple_stepper.h which uses batch writes for real-time stepper control
 * @see motor_stepper.h which uses per-motor writes for position mode
 * @see WIRING_GUIDE.md for physical wiring details
 */

#ifndef MCP23017_H
#define MCP23017_H

#include "hardware/i2c.h"
#include <stdint.h>

// =============================================================================
// I2C BUS CONFIGURATION
// =============================================================================
// These must match the physical wiring on the MechaPico MCB.

/**
 * @defgroup i2c_config I2C Bus Configuration
 * @brief Hardware-level I2C settings for the MCP23017 communication bus.
 * @{
 */

/** @brief Which I2C peripheral on the Pico to use (i2c0 or i2c1).
 *  The MechaPico MCB routes SDA/SCL to i2c0 pins. */
#define MCP23017_I2C_PORT i2c0

/** @brief GPIO pin number for I2C data line (SDA). */
#define MCP23017_I2C_SDA_PIN 4

/** @brief GPIO pin number for I2C clock line (SCL). */
#define MCP23017_I2C_SCL_PIN 5

/** @brief I2C clock speed in Hz. 400kHz is "Fast Mode", the maximum the
 *  MCP23017 supports. At this speed, each byte transfer takes ~25us. */
#define MCP23017_I2C_FREQ 400000

/** @} */ // end of i2c_config group

// =============================================================================
// MCP23017 I2C ADDRESSES
// =============================================================================

/**
 * @defgroup mcp_addresses MCP23017 Chip Addresses
 * @brief I2C addresses for the two MCP23017 chips on the MechaPico MCB.
 *
 * The MCP23017 has 3 address pins (A0, A1, A2) that set its I2C address.
 * The base address is 0x20; each address pin adds to it. On the MechaPico
 * MCB, U6_1 has all address pins LOW (0x20) and U6_2 has A0 HIGH (0x21).
 * @{
 */

/** @brief U6_1: Stepper motor control chip (address pins: A0=0, A1=0, A2=0). */
#define MCP23017_STEPPER_ADDR 0x20

/** @brief U6_2: DC motor and LED control chip (address pins: A0=1, A1=0, A2=0). */
#define MCP23017_DCMOTOR_ADDR 0x21

/** @} */ // end of mcp_addresses group

// =============================================================================
// MCP23017 REGISTER MAP
// =============================================================================

/**
 * @defgroup mcp_registers MCP23017 Register Addresses
 * @brief Internal register addresses for the MCP23017 (IOCON.BANK=0 mode,
 *        sequential addressing).
 *
 * The MCP23017 has two 8-bit ports (A and B), each with its own set of
 * registers for direction, polarity, interrupts, pull-ups, and GPIO state.
 * Think of each port as 8 independent pins you can configure individually.
 *
 * You rarely need to use these directly. The MCP23017 class methods
 * (setPortA, setBitB, etc.) handle register access for you.
 * @{
 */

/** @brief Port A direction register. Each bit: 1=input, 0=output.
 *  On init, all pins are set to output (0x00). */
#define MCP23017_IODIRA 0x00

/** @brief Port B direction register. Same as Port A. */
#define MCP23017_IODIRB 0x01

/** @brief Port A input polarity. 1=inverted (HIGH reads as 0). Not used. */
#define MCP23017_IPOLA 0x02

/** @brief Port B input polarity. Not used. */
#define MCP23017_IPOLB 0x03

/** @brief Port A interrupt-on-change enable. Not used. */
#define MCP23017_GPINTENA 0x04

/** @brief Port B interrupt-on-change enable. Not used. */
#define MCP23017_GPINTENB 0x05

/** @brief Port A default compare value for interrupts. Not used. */
#define MCP23017_DEFVALA 0x06

/** @brief Port B default compare value for interrupts. Not used. */
#define MCP23017_DEFVALB 0x07

/** @brief Port A interrupt control mode. Not used. */
#define MCP23017_INTCONA 0x08

/** @brief Port B interrupt control mode. Not used. */
#define MCP23017_INTCONB 0x09

/** @brief Chip configuration register (shared between ports). */
#define MCP23017_IOCON 0x0A

/** @brief Port A pull-up enable. 1=100k pull-up resistor enabled. Not used. */
#define MCP23017_GPPUA 0x0C

/** @brief Port B pull-up enable. Not used. */
#define MCP23017_GPPUB 0x0D

/** @brief Port A interrupt flag. Shows which pin triggered an interrupt. */
#define MCP23017_INTFA 0x0E

/** @brief Port B interrupt flag. */
#define MCP23017_INTFB 0x0F

/** @brief Port A interrupt capture. Pin values at moment of interrupt. */
#define MCP23017_INTCAPA 0x10

/** @brief Port B interrupt capture. */
#define MCP23017_INTCAPB 0x11

/** @brief Port A GPIO register. Read = pin state, Write = output value. */
#define MCP23017_GPIOA 0x12

/** @brief Port B GPIO register. Read = pin state, Write = output value. */
#define MCP23017_GPIOB 0x13

/** @brief Port A output latch. Holds the last value written to Port A. */
#define MCP23017_OLATA 0x14

/** @brief Port B output latch. Holds the last value written to Port B. */
#define MCP23017_OLATB 0x15

/** @} */ // end of mcp_registers group

// =============================================================================
// U6_1 STEPPER MOTOR PIN MASKS (MCP23017 @ 0x20)
// =============================================================================

/**
 * @defgroup stepper_pins U6_1 Stepper Motor Pin Bit Masks
 * @brief Bit masks for stepper motor control pins on MCP23017 U6_1.
 *
 * Each stepper motor uses 2 pins: STEP (pulsed to move one step) and DIR
 * (HIGH=forward, LOW=reverse). Motors M1-M4 are on Port B; M5 is on Port A.
 *
 * To move Motor 1 forward one step:
 *   1. Set STPR_M1_DIR_BIT HIGH on Port B (direction = forward)
 *   2. Set STPR_M1_STEP_BIT HIGH on Port B (begin pulse)
 *   3. Wait ~5us (minimum pulse width for TMC2209)
 *   4. Set STPR_M1_STEP_BIT LOW on Port B (end pulse, motor steps)
 * @{
 */

/** @brief Motor 1 direction bit. GPB0. HIGH=forward, LOW=reverse. */
#define STPR_M1_DIR_BIT  (1 << 0)
/** @brief Motor 1 step pulse bit. GPB1. Toggle to generate steps. */
#define STPR_M1_STEP_BIT (1 << 1)
/** @brief Motor 2 direction bit. GPB2. */
#define STPR_M2_DIR_BIT  (1 << 2)
/** @brief Motor 2 step pulse bit. GPB3. */
#define STPR_M2_STEP_BIT (1 << 3)
/** @brief Motor 3 direction bit. GPB4. */
#define STPR_M3_DIR_BIT  (1 << 4)
/** @brief Motor 3 step pulse bit. GPB5. */
#define STPR_M3_STEP_BIT (1 << 5)
/** @brief Motor 4 direction bit. GPB6. */
#define STPR_M4_DIR_BIT  (1 << 6)
/** @brief Motor 4 step pulse bit. GPB7. */
#define STPR_M4_STEP_BIT (1 << 7)

/** @} */ // end of stepper_pins (Port B)

/**
 * @defgroup porta_pins U6_1 Port A Pin Bit Masks
 * @brief Bit masks for Port A on MCP23017 U6_1 (Motor 5 + shared controls).
 *
 * Port A carries Motor 5 step/dir, the shared stepper enable signal,
 * microstepping configuration pins (MS1, MS2), SpreadCycle mode, and
 * the PDN/UART mode selector for TMC2209 drivers.
 * @{
 */

/** @brief LED bar indicator 1. GPA0. */
#define LED_BAR_1_BIT     (1 << 0)
/** @brief Motor 5 step pulse bit. GPA1. */
#define STPR_M5_STEP_BIT  (1 << 1)
/** @brief Motor 5 direction bit. GPA2. */
#define STPR_M5_DIR_BIT   (1 << 2)

/** @brief Enable ALL stepper drivers (active LOW!). GPA3.
 *
 *  @warning This pin is ACTIVE LOW. Writing 0 ENABLES the drivers,
 *           writing 1 DISABLES them. This is the opposite of what you
 *           might expect. The enable is shared: all 5 motors turn on or
 *           off together.
 *
 *  @see stepperEnableAll() which handles the inversion for you
 *  @see stepperDisableAll() which handles the inversion for you
 */
#define STPR_ALL_EN_BIT   (1 << 3)

/** @brief Microstepping MS1 pin (shared across all drivers). GPA4.
 *  @see stepperSetMicrostepping() for the truth table */
#define STPR_ALL_MS1_BIT  (1 << 4)

/** @brief Microstepping MS2 pin (shared across all drivers). GPA5.
 *  @see stepperSetMicrostepping() for the truth table */
#define STPR_ALL_MS2_BIT  (1 << 5)

/** @brief SpreadCycle mode select (TMC2209 only). GPA6.
 *  HIGH = SpreadCycle (louder but more torque at high speed).
 *  LOW = StealthChop (quiet, default). */
#define STPR_ALL_SPRD_BIT (1 << 6)

/** @brief PDN_UART mode select (TMC2209 only). GPA7.
 *  HIGH = standalone STEP/DIR mode (our default).
 *  LOW = UART configuration mode (for runtime tuning). */
#define STPR_ALL_PDN_BIT  (1 << 7)

/** @} */ // end of porta_pins group

// =============================================================================
// U6_2 DC MOTOR & LED PIN MASKS (MCP23017 @ 0x21)
// =============================================================================

/**
 * @defgroup dc_pins U6_2 DC Motor and LED Pin Bit Masks
 * @brief Bit masks for DC motor and LED control on MCP23017 U6_2.
 * @{
 */

// Port B: General Purpose 5V I/O (directly exposed on PCB headers)
/** @brief General purpose 5V output bit 0. GPB0. */
#define IO_5V_0_BIT (1 << 0)
/** @brief General purpose 5V output bit 1. GPB1. */
#define IO_5V_1_BIT (1 << 1)
/** @brief General purpose 5V output bit 2. GPB2. */
#define IO_5V_2_BIT (1 << 2)
/** @brief General purpose 5V output bit 3. GPB3. */
#define IO_5V_3_BIT (1 << 3)
/** @brief General purpose 5V output bit 4. GPB4. */
#define IO_5V_4_BIT (1 << 4)
/** @brief General purpose 5V output bit 5. GPB5. */
#define IO_5V_5_BIT (1 << 5)
/** @brief General purpose 5V output bit 6. GPB6. */
#define IO_5V_6_BIT (1 << 6)
/** @brief General purpose 5V output bit 7. GPB7. */
#define IO_5V_7_BIT (1 << 7)

// Port A: DC Motor Control & LED Bar
/** @brief DC Motor 4 negative terminal. GPA0. Part of H-bridge pair. */
#define IN_MOT_4N_BIT (1 << 0)
/** @brief DC Motor 4 positive terminal. GPA1. Part of H-bridge pair. */
#define IN_MOT_4P_BIT (1 << 1)
/** @brief DC Motor 3 negative terminal. GPA2. Part of H-bridge pair. */
#define IN_MOT_3N_BIT (1 << 2)
/** @brief DC Motor 3 positive terminal. GPA3. Part of H-bridge pair. */
#define IN_MOT_3P_BIT (1 << 3)
/** @brief LED bar indicator 2. GPA4. */
#define LED_BAR_2_BIT (1 << 4)
/** @brief LED bar indicator 3. GPA5. */
#define LED_BAR_3_BIT (1 << 5)
/** @brief LED bar indicator 4. GPA6. */
#define LED_BAR_4_BIT (1 << 6)
/** @brief LED bar indicator 5. GPA7. */
#define LED_BAR_5_BIT (1 << 7)

/** @} */ // end of dc_pins group

// =============================================================================
// STEPPER MOTOR INDEX MAPPING
// =============================================================================

/**
 * @brief Pin configuration for one stepper motor's step/dir signals.
 *
 * Maps a motor index (0-4) to its physical MCP23017 pin bit masks and port.
 * Used by convenience functions (stepperPulse, stepperSetDirection) to find
 * the correct bits to toggle for a given motor number.
 *
 * @see STEPPER_PINS[] lookup table below
 */
typedef struct {
  uint8_t stepBit; /**< Bit mask for the STEP pin (e.g., STPR_M1_STEP_BIT). */
  uint8_t dirBit;  /**< Bit mask for the DIR pin (e.g., STPR_M1_DIR_BIT). */
  uint8_t port;    /**< Which MCP23017 port: 0=Port A, 1=Port B. Motors M1-M4
                        are on Port B (1), Motor M5 is on Port A (0). */
} StepperPinConfig;

/**
 * @brief Lookup table mapping motor index (0-4) to MCP23017 pins.
 *
 * Index 0 = Motor M1 (Port B, bits 0-1)
 * Index 1 = Motor M2 (Port B, bits 2-3)
 * Index 2 = Motor M3 (Port B, bits 4-5)
 * Index 3 = Motor M4 (Port B, bits 6-7)
 * Index 4 = Motor M5 (Port A, bits 1-2)
 *
 * @note This table is declared `static` so each .cpp file that includes
 *       this header gets its own copy. This avoids linker "multiple
 *       definition" errors.
 */
static const StepperPinConfig STEPPER_PINS[5] = {
    {STPR_M1_STEP_BIT, STPR_M1_DIR_BIT, 1}, // M1: Port B
    {STPR_M2_STEP_BIT, STPR_M2_DIR_BIT, 1}, // M2: Port B
    {STPR_M3_STEP_BIT, STPR_M3_DIR_BIT, 1}, // M3: Port B
    {STPR_M4_STEP_BIT, STPR_M4_DIR_BIT, 1}, // M4: Port B
    {STPR_M5_STEP_BIT, STPR_M5_DIR_BIT, 0}, // M5: Port A
};

// =============================================================================
// MCP23017 DRIVER CLASS
// =============================================================================

/**
 * @brief MCP23017 I2C GPIO expander driver.
 *
 * Provides read/write access to the MCP23017's two 8-bit ports (A and B).
 * The driver caches the port output values locally (portA_, portB_) to avoid
 * unnecessary I2C reads before writes. This is safe because the firmware is
 * the only master on the I2C bus.
 *
 * There are two global instances of this class:
 *   - `mcpStepper` at address 0x20 (stepper motor control)
 *   - `mcpDCMotor` at address 0x21 (DC motors and LEDs)
 *
 * @note The I2C bus is initialized once (shared between both instances).
 *       The first instance to call init() sets up the bus; the second
 *       skips bus init and only configures its own chip.
 */
class MCP23017 {
public:
  /**
   * @brief Construct a driver for a specific MCP23017 chip.
   *
   * @param addr 7-bit I2C address: 0x20 for stepper chip (U6_1),
   *             0x21 for DC motor/LED chip (U6_2).
   *
   * @note Construction does NOT touch the hardware. Call init() to
   *       actually configure the chip.
   */
  explicit MCP23017(uint8_t addr);

  /**
   * @brief Initialize the I2C bus and configure the MCP23017 chip.
   *
   * Sets up the I2C peripheral (if not already done), then configures
   * all 16 pins as outputs (IODIRA=0x00, IODIRB=0x00) and drives them
   * all LOW (safe starting state: motors off, LEDs off).
   *
   * @return true if the chip responded to I2C correctly.
   * @return false if no response (chip missing, wrong address, wiring error).
   *         Check the serial log for "[MCP] ERROR" messages.
   *
   * @note Safe to call multiple times; subsequent calls are ignored.
   * @warning If this returns false, stepper motors will not work. Check
   *          I2C wiring (SDA, SCL, power, ground).
   */
  bool init();

  /**
   * @brief Write a value to an MCP23017 internal register.
   *
   * Low-level register access. You normally don't need this. Use setPortA(),
   * setPortB(), setBitA(), setBitB() instead.
   *
   * @param reg  Register address (e.g., MCP23017_GPIOA).
   * @param value 8-bit value to write to the register.
   * @return true if the I2C write was acknowledged by the chip.
   * @return false if the I2C write failed (bus error, chip not responding).
   */
  bool writeRegister(uint8_t reg, uint8_t value);

  /**
   * @brief Read a value from an MCP23017 internal register.
   *
   * Low-level register access. Returns the current value of the specified
   * register.
   *
   * @param reg Register address (e.g., MCP23017_GPIOB).
   * @return The 8-bit register value, or 0 on I2C error.
   *
   * @note Most code uses the cached portA_/portB_ values via getPortA()/
   *       getPortB() instead of reading from the chip, which is faster.
   */
  uint8_t readRegister(uint8_t reg);

  /**
   * @brief Set all 8 pins of Port A simultaneously.
   *
   * Writes the value to the GPIO register AND updates the internal cache.
   *
   * @param value 8-bit value where each bit controls one pin.
   *              Bit 0 = GPA0, Bit 7 = GPA7. 1=HIGH, 0=LOW.
   */
  void setPortA(uint8_t value);

  /**
   * @brief Set all 8 pins of Port B simultaneously.
   *
   * Used heavily by simple_stepper.cpp for batch step pulse generation.
   * Writes the value to the GPIO register AND updates the internal cache.
   *
   * @param value 8-bit value where each bit controls one pin.
   *              Bit 0 = GPB0, Bit 7 = GPB7. 1=HIGH, 0=LOW.
   *
   * @see simple_stepper_update() which writes step+dir patterns here
   */
  void setPortB(uint8_t value);

  /**
   * @brief Get the cached Port A output value (no I2C read).
   * @return The last value written to Port A by setPortA() or setBitA().
   */
  uint8_t getPortA() const { return portA_; }

  /**
   * @brief Get the cached Port B output value (no I2C read).
   * @return The last value written to Port B by setPortB() or setBitB().
   */
  uint8_t getPortB() const { return portB_; }

  /**
   * @brief Set or clear a single bit on Port A.
   *
   * Reads the cached value, modifies one bit, writes the result.
   * Only one I2C write is needed (no read-modify-write from chip).
   *
   * @param bit   Bit mask to modify (e.g., STPR_ALL_EN_BIT).
   * @param state true=set the bit HIGH, false=set the bit LOW.
   */
  void setBitA(uint8_t bit, bool state);

  /**
   * @brief Set or clear a single bit on Port B.
   *
   * @param bit   Bit mask to modify (e.g., STPR_M1_STEP_BIT).
   * @param state true=set the bit HIGH, false=set the bit LOW.
   */
  void setBitB(uint8_t bit, bool state);

  /**
   * @brief Toggle (flip) a single bit on Port B.
   *
   * If the bit is HIGH, it becomes LOW, and vice versa. Useful for
   * generating step pulses: toggle HIGH (step begins), then toggle LOW
   * (step completes).
   *
   * @param bit Bit mask to toggle (e.g., STPR_M1_STEP_BIT).
   */
  void toggleBitB(uint8_t bit);

  /**
   * @brief Toggle (flip) a single bit on Port A.
   * @param bit Bit mask to toggle (e.g., STPR_M5_STEP_BIT).
   */
  void toggleBitA(uint8_t bit);

private:
  uint8_t addr_;        /**< 7-bit I2C address of this MCP23017 chip. */
  uint8_t portA_;       /**< Cached value of Port A outputs. Updated on every
                             write so we don't need to read the chip first. */
  uint8_t portB_;       /**< Cached value of Port B outputs. */
  bool initialized_;    /**< true after init() completes successfully. */

  static bool i2cInitialized_; /**< Shared flag: true after the I2C bus
                                    peripheral has been set up. Only done once
                                    even with multiple MCP23017 instances. */

  /**
   * @brief Initialize the Pico's I2C peripheral (one-time setup).
   *
   * Configures the I2C hardware, sets GPIO pin functions for SDA and SCL,
   * and enables internal pull-ups. Called automatically by the first
   * MCP23017 instance to call init(). Subsequent instances skip this.
   */
  static void initI2C();
};

// =============================================================================
// GLOBAL INSTANCES
// =============================================================================

/**
 * @brief Global MCP23017 driver for the stepper control chip (U6_1 @ 0x20).
 *
 * Used by simple_stepper.cpp (Core 1) for batch step pulse generation
 * and by motor_manager.cpp (Core 0) for initialization and emergency stop.
 *
 * @warning Both cores access this instance. I2C bus contention is possible
 *          during emergency stop (Core 0 calling motors_stop_all while
 *          Core 1 is generating pulses). This is acceptable for safety.
 */
extern MCP23017 mcpStepper;

/**
 * @brief Global MCP23017 driver for the DC motor/LED chip (U6_2 @ 0x21).
 *
 * Used by DC motor control and LED status indicators. Only accessed from
 * Core 0 (no cross-core contention).
 */
extern MCP23017 mcpDCMotor;

// =============================================================================
// CONVENIENCE FUNCTIONS FOR STEPPER CONTROL
// =============================================================================

/**
 * @defgroup stepper_convenience Stepper Motor Convenience Functions
 * @brief High-level functions for common stepper operations.
 *
 * These wrap the low-level MCP23017 register writes into easy-to-use
 * functions. If you're not writing a custom motor driver, use these
 * instead of manipulating registers directly.
 * @{
 */

/**
 * @brief Enable all stepper motor drivers (power on).
 *
 * Sets the shared STPR_ALL_EN pin LOW (active-low logic). After this call,
 * all stepper drivers are powered and ready to receive step pulses.
 *
 * @note The enable is ACTIVE LOW. This function handles the inversion,
 *       so you don't need to worry about it.
 * @warning Enabling stepper drivers causes them to energize their coils
 *          and hold position (they draw current even when not moving).
 * @see stepperDisableAll() to turn off and save power
 */
void stepperEnableAll();

/**
 * @brief Disable all stepper motor drivers (power off).
 *
 * Sets the shared STPR_ALL_EN pin HIGH (active-low logic). After this call,
 * stepper coils are de-energized. Motors can now be turned by hand but
 * will NOT hold position.
 *
 * @note Useful for power saving when the robot is idle. The firmware does
 *       NOT call this automatically; drivers stay enabled once initialized.
 */
void stepperDisableAll();

/**
 * @brief Set the microstepping mode for all stepper drivers.
 *
 * Configures the MS1 and MS2 pins, which are shared across all stepper
 * drivers on the board. The resulting microstep resolution depends on
 * the driver type:
 *
 * TMC2209 truth table:
 * | MS1 | MS2 | Microsteps |
 * |-----|-----|------------|
 * |  0  |  0  |     8      |
 * |  1  |  0  |     2      |
 * |  0  |  1  |     4      |
 * |  1  |  1  |    16      |
 *
 * A4988 truth table (different from TMC2209!):
 * | MS1 | MS2 | Microsteps |
 * |-----|-----|------------|
 * |  0  |  0  |  Full step |
 * |  1  |  0  |   1/2      |
 * |  0  |  1  |   1/4      |
 * |  1  |  1  |   1/8      |
 *
 * @param ms1 Desired state of the MS1 pin (true=HIGH, false=LOW).
 * @param ms2 Desired state of the MS2 pin (true=HIGH, false=LOW).
 *
 * @warning The TMC2209 and A4988 have DIFFERENT truth tables for the same
 *          MS1/MS2 states. Double-check which driver you're using!
 * @see STEPPER_MICROSTEPPING in project_config.h for the configured value
 */
void stepperSetMicrostepping(bool ms1, bool ms2);

/**
 * @brief Set the direction for a single stepper motor.
 *
 * @param motorIndex Motor index 0-4 (0=M1 through 4=M5).
 * @param forward    true = forward (DIR pin HIGH),
 *                   false = reverse (DIR pin LOW).
 *
 * @note For batched direction setting (multiple motors at once), use
 *       stepperSetDirectionBatch() instead.
 */
void stepperSetDirection(uint8_t motorIndex, bool forward);

/**
 * @brief Toggle the STEP pin for a single motor (half-pulse).
 *
 * Flips the STEP pin from its current state (LOW->HIGH or HIGH->LOW).
 * A complete step requires TWO toggles: HIGH then LOW (or LOW then HIGH).
 *
 * @param motorIndex Motor index 0-4 (0=M1 through 4=M5).
 *
 * @note For most applications, use stepperPulse() which does both toggles
 *       with the correct timing.
 */
void stepperToggleStep(uint8_t motorIndex);

/**
 * @brief Generate a complete step pulse (HIGH then LOW) for one motor.
 *
 * Sets the STEP pin HIGH, waits for the minimum pulse width (~5us for
 * TMC2209), then sets it LOW. The motor advances one microstep.
 *
 * @param motorIndex Motor index 0-4 (0=M1 through 4=M5).
 *
 * @note Each pulse requires 2 I2C writes (~50us total). For real-time
 *       multi-motor control, use stepperPulseBatchPortB() instead.
 * @see stepperPulseBatchPortB() for the faster batched version
 */
void stepperPulse(uint8_t motorIndex);

/**
 * @brief Generate step pulses for multiple motors in a SINGLE I2C write.
 *
 * This is the high-performance version of stepperPulse(). Instead of
 * pulsing each motor's STEP pin individually (2 I2C writes per motor),
 * it combines all STEP bits into one Port B write, then clears them in
 * a second write. Total: 2 I2C writes regardless of how many motors.
 *
 * @param stepMask Bitmask of STEP bits to pulse. Combine with bitwise OR.
 *                 Example: `STPR_M1_STEP_BIT | STPR_M3_STEP_BIT` pulses
 *                 motors 1 and 3 simultaneously.
 *
 * @note Only works for motors M1-M4 (Port B). Motor M5 is on Port A
 *       and must be pulsed separately.
 * @note This is what simple_stepper_update() uses internally for its
 *       500us real-time loop.
 * @see simple_stepper_update() which calls this every 500us
 */
void stepperPulseBatchPortB(uint8_t stepMask);

/**
 * @brief Set direction for multiple motors in a SINGLE I2C write.
 *
 * Efficiently sets multiple motor directions at once using cached
 * read-modify-write on Port B.
 *
 * @param setHighMask Bitmask of direction bits to set HIGH (forward).
 *                    Example: STPR_M1_DIR_BIT | STPR_M2_DIR_BIT.
 * @param setLowMask  Bitmask of direction bits to set LOW (reverse).
 *                    Example: STPR_M3_DIR_BIT.
 *
 * @note Bits not present in either mask are left unchanged.
 */
void stepperSetDirectionBatch(uint8_t setHighMask, uint8_t setLowMask);

/** @} */ // end of stepper_convenience group

#endif // MCP23017_H
