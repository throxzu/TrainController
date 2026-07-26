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
//
// Wiring convention: sensors run sequentially, entry then exit per section,
// filling each PCF8574 pin 0→7 before moving to the next I2C address.
// Sensor index i = (section-1)*2 + (0 entry | 1 exit)
//   addr = 0x20 + i/8,  pin = i%8
// All 28 sensors are wired and enabled across four PCF8574s: 0x20, 0x21 and
// 0x22 are fully populated; 0x23 uses only pins 0–3 (sections 13–14).
// ---------------------------------------------------------------------------
static const SectionSensors_t SECTION_SENSORS[NUM_SECTIONS] = {
//             entry                    exit
    {{0x20, 0}, {0x20, 1}},                          // section  1
    {{0x20, 2}, {0x20, 3}},                          // section  2
    {{0x20, 4}, {0x20, 5}},                          // section  3
    {{0x20, 6}, {0x20, 7}},                          // section  4
    {{0x21, 0}, {0x21, 1}},                          // section  5
    {{0x21, 2}, {0x21, 3}},                          // section  6
    {{0x21, 4}, {0x21, 5}},                          // section  7
    {{0x21, 6}, {0x21, 7}},                          // section  8
    {{0x22, 0}, {0x22, 1}},                          // section  9
    {{0x22, 2}, {0x22, 3}},                          // section 10
    {{0x22, 4}, {0x22, 5}},                          // section 11
    {{0x22, 6}, {0x22, 7}},                          // section 12
    {{0x23, 0}, {0x23, 1}},                          // section 13
    {{0x23, 2}, {0x23, 3}},                          // section 14
};

#define DETECT_POLL_MS  20

#endif // DETECT_CONFIG_H
