#ifndef DETECT_CONFIG_H
#define DETECT_CONFIG_H

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "hal/adc_types.h"

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

// ---------------------------------------------------------------------------
// LED outputs
//
// The 28 sensors occupy 28 of the 64 pins across the eight PCF8574s, leaving 36
// free for LED switching: 0x23 pins 4–7, plus all of 0x24–0x27.
//
// LEDs are wired active-LOW — anode to VCC through a series resistor, cathode
// to the pin — because the PCF8574 sinks ~25 mA but sources only ~100 uA, so it
// can pull an LED low but cannot drive one high. Pin LOW = lit.
//
// Writing a PCF8574 sets all eight pins at once, so the LED write must keep a
// 1 in every sensor bit to leave those pins in input mode. The output shadow
// byte in the detection task handles that.
// ---------------------------------------------------------------------------
#define NUM_LEDS  1

// Reuses SensorPin_t — it is just an {addr, pin} pair.
static const SensorPin_t LED_PINS[NUM_LEDS] = {
    {0x23, 4},   // LED 1 — first free pin after section 14's exit sensor
};

// ---------------------------------------------------------------------------
// Cooling fans
//
// Four 5 V fans switched together low-side by an IRLZ44N: GPIO18 drives the
// gate through 100 R, a 10 k pulldown holds the gate low so the fans stay off
// through boot and reset, and a 1N5392 across the fans catches the inductive
// kick when the FET turns off.
//
// Driven by LEDC hardware PWM rather than bit-banging. 25 kHz is the standard
// DC-fan drive frequency — above hearing, so the fans do not whine. The LEDC
// source clock is 80 MHz and needs freq * 2^resolution <= 80 MHz, which caps
// 25 kHz at 11 bits (25000 * 2048 = 51.2 MHz).
// ---------------------------------------------------------------------------
// All four fans hang off the one FET, so they are a single control channel.
#define NUM_FANS          1

#define FAN_PWM_GPIO      GPIO_NUM_18
#define FAN_PWM_FREQ_HZ   25000
#define FAN_PWM_RES_BITS  LEDC_TIMER_11_BIT
// LEDC duty runs 0..2^resolution inclusive, where the top value means
// continuously on. Scale intermediate speeds against 2047, but use 2048 for
// full power so 100% is true DC with no residual chopping.
#define FAN_PWM_MAX_DUTY  2047            // (1 << 11) - 1
#define FAN_PWM_FULL_DUTY 2048            // (1 << 11) — always on
#define FAN_PWM_TIMER     LEDC_TIMER_0
#define FAN_PWM_CHANNEL   LEDC_CHANNEL_0
#define FAN_PWM_MODE      LEDC_HIGH_SPEED_MODE

// These fans keep spinning at 95% once moving but cannot break away from rest
// there — and the more fans on the FET, the worse it gets, because the extra
// current widens the drop across a gate driven at only 3.3 V. So every speed
// below full gets a brief burst at 100% to get them turning, then settles.
// Threshold is 100 rather than 95 so it still applies if the Low setting is
// ever nudged up; only Max (which is already full power) skips the kick.
#define FAN_KICKSTART_PCT 100
#define FAN_KICKSTART_MS  600

// ---------------------------------------------------------------------------
// KY-013 temperature sensor
//
// The module is an NTC thermistor in a divider with a 10 k resistor, powered
// from 3.3 V so its output cannot exceed the ADC input range. It must sit on
// an ADC1 channel: ADC2 is shared with the Wi-Fi radio and its reads fail once
// Wi-Fi is up, which for this firmware is always.
//
// GPIO34 = ADC1 channel 6, input-only.
//
// 11 dB attenuation gives roughly a 0-3.1 V span; the default 0 dB would clip
// above ~1.1 V and lose most of the range.
// ---------------------------------------------------------------------------
#define TEMP_ADC_CHANNEL   ADC_CHANNEL_6      // GPIO34
#define TEMP_ADC_ATTEN     ADC_ATTEN_DB_11
#define TEMP_SAMPLES       32                 // averaged per reading — the ESP32 ADC is noisy
#define TEMP_PUBLISH_MS    5000

// Automatic fan control. Two thresholds rather than one: switching on and off
// at the same temperature would make the fans chatter every few seconds while
// hovering at the setpoint. Fans start above ON and do not stop until the
// temperature has dropped a degree below it.
#define FAN_TEMP_ON_C      28.0f
#define FAN_TEMP_OFF_C     27.0f

// Divider maths. On this module the thermistor sits between the signal pin and
// ground, with the fixed resistor up to VCC, so the voltage FALLS as it warms
// — verified by heating the sensor. (Boards wired the other way round need
// this set to 1, or the temperature moves backwards.)
#define TEMP_NTC_HIGH_SIDE 0
#define TEMP_SERIES_OHMS   10000.0f
#define TEMP_SUPPLY_MV     3300.0f

// Beta-equation constants for the KY-013's 10 k NTC.
#define TEMP_NOMINAL_OHMS  10000.0f
#define TEMP_NOMINAL_K     298.15f            // 25 C
#define TEMP_BETA          3950.0f

#define DETECT_POLL_MS  20

#endif // DETECT_CONFIG_H
