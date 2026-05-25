#ifndef DETECT_CONFIG_H
#define DETECT_CONFIG_H

#include "driver/gpio.h"

// ---------------------------------------------------------------------------
// I2C bus
// ---------------------------------------------------------------------------
#define DETECT_I2C_SDA  GPIO_NUM_21
#define DETECT_I2C_SCL  GPIO_NUM_22

// ---------------------------------------------------------------------------
// Sections
// ---------------------------------------------------------------------------
#define NUM_SECTIONS      14
#define SENSOR_NOT_WIRED  0xFF   // addr sentinel — sensor not yet connected

// Each section has two A3144 Hall sensors. The sensor output is active-LOW
// (open-collector): magnet present → PCF8574 pin reads LOW → triggered = true.
// Write 0xFF to each PCF8574 before reading to enable input mode.
typedef struct {
    uint8_t addr;   // PCF8574 I2C address (0x20–0x27); SENSOR_NOT_WIRED = skip
    uint8_t pin;    // PCF8574 pin 0–7
} SensorPin_t;

typedef struct {
    SensorPin_t entry;  // fires when train nose enters the section
    SensorPin_t exit;   // fires when train tail leaves the section
} SectionSensors_t;

// ---------------------------------------------------------------------------
// Section sensor mapping — fill in as hardware is wired.
// Current test setup: section 1 entry only (0x20 pin 0), all others unwired.
// ---------------------------------------------------------------------------
static const SectionSensors_t SECTION_SENSORS[NUM_SECTIONS] = {
//             entry                    exit
    {{0x20, 0}, {SENSOR_NOT_WIRED, 0}},   // section  1
    {{SENSOR_NOT_WIRED, 0}, {SENSOR_NOT_WIRED, 0}},  // section  2
    {{SENSOR_NOT_WIRED, 0}, {SENSOR_NOT_WIRED, 0}},  // section  3
    {{SENSOR_NOT_WIRED, 0}, {SENSOR_NOT_WIRED, 0}},  // section  4
    {{SENSOR_NOT_WIRED, 0}, {SENSOR_NOT_WIRED, 0}},  // section  5
    {{SENSOR_NOT_WIRED, 0}, {SENSOR_NOT_WIRED, 0}},  // section  6
    {{SENSOR_NOT_WIRED, 0}, {SENSOR_NOT_WIRED, 0}},  // section  7
    {{SENSOR_NOT_WIRED, 0}, {SENSOR_NOT_WIRED, 0}},  // section  8
    {{SENSOR_NOT_WIRED, 0}, {SENSOR_NOT_WIRED, 0}},  // section  9
    {{SENSOR_NOT_WIRED, 0}, {SENSOR_NOT_WIRED, 0}},  // section 10
    {{SENSOR_NOT_WIRED, 0}, {SENSOR_NOT_WIRED, 0}},  // section 11
    {{SENSOR_NOT_WIRED, 0}, {SENSOR_NOT_WIRED, 0}},  // section 12
    {{SENSOR_NOT_WIRED, 0}, {SENSOR_NOT_WIRED, 0}},  // section 13
    {{SENSOR_NOT_WIRED, 0}, {SENSOR_NOT_WIRED, 0}},  // section 14
};

#define DETECT_POLL_MS  20

#endif // DETECT_CONFIG_H
