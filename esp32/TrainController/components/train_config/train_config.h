#ifndef TRAIN_CONFIG_H
#define TRAIN_CONFIG_H

#include "driver/gpio.h"
#include "driver/ledc.h"

// ---------------------------------------------------------------------------
// I2C bus (shared by all PCF8574 expanders)
// ---------------------------------------------------------------------------
#define TRAIN_I2C_SDA  GPIO_NUM_21
#define TRAIN_I2C_SCL  GPIO_NUM_22

// ---------------------------------------------------------------------------
// PCF8574 expanders
// 7 chips at consecutive addresses 0x20–0x26 = 56 control pins total
// ---------------------------------------------------------------------------
#define NUM_EXPANDERS       7
#define EXPANDER_BASE_ADDR  0x20

// ---------------------------------------------------------------------------
// Solenoid safety timing
// Pulse duration for turnout coils — long enough to throw, short enough
// not to burn. Coils are always returned to de-energized state after this.
// ---------------------------------------------------------------------------
#define SOLENOID_PULSE_MS  30

// ---------------------------------------------------------------------------
// Layout counts
// ---------------------------------------------------------------------------
#define NUM_SECTIONS  14
#define NUM_TURNOUTS   6

// ---------------------------------------------------------------------------
// LEDC (PWM) shared configuration for all section speed channels
// 10-bit resolution → duty range 0–1023
// ---------------------------------------------------------------------------
#define TRAIN_LEDC_TIMER       LEDC_TIMER_0
#define TRAIN_LEDC_FREQ_HZ     10000
#define TRAIN_LEDC_RESOLUTION  LEDC_TIMER_10_BIT

// ---------------------------------------------------------------------------
// Section (track power district) configuration
//
// enablePin   : GPIO connected to L298N ENA or ENB (PWM speed control)
// ledcChannel : LEDC channel (0–7 for high-speed mode, 0–5 for low-speed)
// ledcMode    : LEDC_HIGH_SPEED_MODE (sections 1–8) or LEDC_LOW_SPEED_MODE
// dirAddr     : PCF8574 I2C address that holds the direction pins
// fwdPin      : PCF8574 pin set LOW for forward direction
// revPin      : PCF8574 pin set LOW for reverse direction
//
// NOTE: GPIO_NUM_33 is currently also defined as PUSH_BUTTON_PIN in
//       app_main.cpp (section 2). Reassign one before connecting hardware.
//
// TODO: update dirAddr / fwdPin / revPin for each section once wired.
// ---------------------------------------------------------------------------
typedef struct {
    gpio_num_t     enablePin;
    ledc_channel_t ledcChannel;
    ledc_mode_t    ledcMode;
    uint8_t        dirAddr;
    uint8_t        fwdPin;
    uint8_t        revPin;
} SectionConfig_t;

static const SectionConfig_t SECTION_CONFIG[NUM_SECTIONS] = {
//   enablePin      ledcCh              ledcMode                dirAddr  fwd  rev
    {GPIO_NUM_32,  LEDC_CHANNEL_0,  LEDC_HIGH_SPEED_MODE,     0x26,    0,   1},  // section  1
    {GPIO_NUM_33,  LEDC_CHANNEL_1,  LEDC_HIGH_SPEED_MODE,     0x26,    2,   3},  // section  2
    {GPIO_NUM_25,  LEDC_CHANNEL_2,  LEDC_HIGH_SPEED_MODE,     0x26,    4,   5},  // section  3
    {GPIO_NUM_26,  LEDC_CHANNEL_3,  LEDC_HIGH_SPEED_MODE,     0x26,    6,   7},  // section  4
    {GPIO_NUM_27,  LEDC_CHANNEL_4,  LEDC_HIGH_SPEED_MODE,     0x25,    0,   1},  // section  5
    {GPIO_NUM_14,  LEDC_CHANNEL_5,  LEDC_HIGH_SPEED_MODE,     0x25,    2,   3},  // section  6
    {GPIO_NUM_16,  LEDC_CHANNEL_6,  LEDC_HIGH_SPEED_MODE,     0x25,    4,   5},  // section  7
    {GPIO_NUM_13,  LEDC_CHANNEL_7,  LEDC_HIGH_SPEED_MODE,     0x25,    6,   7},  // section  8
    {GPIO_NUM_23,  LEDC_CHANNEL_0,  LEDC_LOW_SPEED_MODE,      0x24,    0,   1},  // section  9
    {GPIO_NUM_19,  LEDC_CHANNEL_1,  LEDC_LOW_SPEED_MODE,      0x24,    2,   3},  // section 10
    {GPIO_NUM_17,  LEDC_CHANNEL_2,  LEDC_LOW_SPEED_MODE,      0x24,    4,   5},  // section 11
    {GPIO_NUM_4,   LEDC_CHANNEL_3,  LEDC_LOW_SPEED_MODE,      0x24,    6,   7},  // section 12
    {GPIO_NUM_18,  LEDC_CHANNEL_4,  LEDC_LOW_SPEED_MODE,      0x23,    0,   1},  // section 13
    {GPIO_NUM_15,  LEDC_CHANNEL_5,  LEDC_LOW_SPEED_MODE,      0x23,    2,   3},  // section 14
};

// ---------------------------------------------------------------------------
// Turnout configuration
//
// i2cAddr    : PCF8574 address that controls this turnout's solenoids
// straightPin: PCF8574 pin pulsed LOW to throw to straight position
// divergePin : PCF8574 pin pulsed LOW to throw to diverge position
//
// Both pins rest HIGH (relay off). Only one pin is ever pulsed at a time.
// The solenoid is de-energized automatically after SOLENOID_PULSE_MS.
//
// TODO: update i2cAddr / straightPin / divergePin for each turnout once wired.
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t i2cAddr;
    uint8_t straightPin;
    uint8_t divergePin;
} TurnoutConfig_t;

static const TurnoutConfig_t TURNOUT_CONFIG[NUM_TURNOUTS] = {
//   addr   straight  diverge
    {0x22,  6,        7      },  // turnout 1
    {0x22,  4,        5      },  // turnout 2
    {0x22,  2,        3      },  // turnout 3
    {0x22,  0,        1      },  // turnout 4
    {0x23,  6,        7      },  // turnout 5
    {0x23,  4,        5      },  // turnout 6 (X)
};

#endif // TRAIN_CONFIG_H
