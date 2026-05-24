# TrainDetector — Arduino Mega 2560 Firmware

Arduino sketch for train presence detection via 14 ACS712 current sensors.
Drives PCF8574 I/O expander pins that the ESP32 reads over I2C.

## Hardware

- **MCU**: Arduino Mega 2560
- **Sensors**: 14× ACS712 current sensor modules (one per track section)
- **Outputs**: Digital pins D22–D35 wired directly to PCF8574 expander pins

## Detection principle

The ACS712 outputs VCC/2 (~2.5 V, ADC ~512) with no current flowing.
When a train is present on a section, motor current shifts the output voltage.
The Mega detects deviations larger than `DETECTION_THRESHOLD` ADC counts and
drives the corresponding output pin LOW (train present) or HIGH (clear).

## Pin mapping

### ACS712 sensors → Mega analog inputs

| Section | Analog pin |
|---------|-----------|
| 1–7     | A0–A6     |
| 8–14    | A7–A13    |

### Mega digital outputs → PCF8574 expander pins

| Section | Mega pin | PCF8574 | PCF pin |
|---------|----------|---------|---------|
| 1       | D22      | 0x20    | 0       |
| 2       | D23      | 0x20    | 1       |
| 3       | D24      | 0x20    | 2       |
| 4       | D25      | 0x20    | 3       |
| 5       | D26      | 0x20    | 4       |
| 6       | D27      | 0x20    | 5       |
| 7       | D28      | 0x20    | 6       |
| 8       | D29      | 0x20    | 7       |
| 9       | D30      | 0x21    | 0       |
| 10      | D31      | 0x21    | 1       |
| 11      | D32      | 0x21    | 2       |
| 12      | D33      | 0x21    | 3       |
| 13      | D34      | 0x21    | 4       |
| 14      | D35      | 0x21    | 5       |

PCF8574 0x21 pins 6–7 remain unused (were reserved for turnout 6, now removed).

## Configuration (`detector_config.h`)

| Constant             | Default | Meaning                                      |
|----------------------|---------|----------------------------------------------|
| `DETECTION_THRESHOLD`| 15      | ADC counts of deviation to register occupied |
| `RECAL_INTERVAL_MS`  | 60000   | Baseline recalibration interval (ms)         |
| `SAMPLE_COUNT`       | 8       | Analog samples averaged per reading          |

Increase `DETECTION_THRESHOLD` to reduce false positives from electrical noise.
Decrease it for greater sensitivity (may increase false positives).

## Build & Upload

Open `TrainDetector.ino` in Arduino IDE 2.x.
Select board: **Arduino Mega or Mega 2560** — port: whichever COM the Mega is on.

## Serial output

115200 baud. Reports `Section N: OCCUPIED` / `Section N: clear` on state changes,
and `Calibration done` after each baseline sample.

## ESP32 integration

The ESP32 (`turnout_control` task) reads PCF8574 0x20 and 0x21 to get the
detection bitmask. A LOW bit = train present. The ESP32 can then publish
`train/detection` MQTT messages to the web UI.
This ESP32-side polling is not yet implemented — see TODO below.

## TODO

- Implement detection polling in ESP32 `turnout_control_task`
- Publish `train/detection` MQTT topic from ESP32
- Display section occupancy in web UI
- Tune `DETECTION_THRESHOLD` once wired (use Serial monitor to observe ADC readings)
