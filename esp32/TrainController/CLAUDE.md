# TrainController — ESP32 Firmware

ESP-IDF v5.1.2 project for an ESP32 model railway controller. C/C++, FreeRTOS, MQTT over Wi-Fi.

## Build & Flash

```powershell
idf.py build
idf.py -p COM<N> flash monitor
```

The ESP-IDF environment must be sourced first. Python is at:
`C:\Users\Darker\.espressif\python_env\idf5.1_py3.11_env\Scripts\python.exe`

To invoke idf.py without the environment sourced, prefix with the full Python path.

## Project Structure

```
main/
  app_main.cpp          Entry point, MQTT client, topic dispatch
  turnout_control.cpp   FreeRTOS task — drives solenoid turnouts via PCF8574
  GeneralUtils.cpp      Misc helpers

components/
  I2C/                  I2C master wrapper (uses esp_err_to_name, not GeneralUtils)
  PCF8574/              PCF8574 I/O expander driver
  motor_l298n/          L298N PWM speed control — FreeRTOS task consuming SectionCmd_t queue
  train_config/         Header-only — pin assignments, section and turnout config tables
  protocol_examples_common/  Wi-Fi connection helper (local copy, not from IDF_PATH)
```

## Layout Constants

Defined in `components/train_config/train_config.h`:
- 14 track sections (NUM_SECTIONS), 7 turnouts (NUM_TURNOUTS)
- 7× PCF8574 at addresses 0x20–0x26
- I2C: SDA GPIO21, SCL GPIO22
- LEDC 10 kHz, 10-bit — high-speed mode for sections 1–8, low-speed for 9–14

## MQTT Topics

ESP32 subscribes to `train/#` with QoS 0.

| Topic | Payload |
|---|---|
| `train/section/{1–14}` | `{"speed":0–100,"direction":"forward"\|"reverse"}` |
| `train/turnout/{1–7}` | `{"position":"straight"\|"diverge"}` |

MQTT credentials (hardcoded in app_main.cpp): username `esp32`, password `password123`.

## Component Dependency Rules

- Components **cannot** depend on `main/`. The I2C component previously used `GeneralUtils.h` from main — that circular dependency was removed. Use `esp_err_to_name()` instead.
- All inter-component dependencies must be declared explicitly in `idf_component_register(... REQUIRES ...)`.
- ESP-IDF 5.x strictly enforces REQUIRES — missing entries cause build failures.

## Git

Only push to git after a successful `idf.py build`. Never push a build that fails to compile.

## Key Constraints

- Target is **ESP32 classic only** — uses `LEDC_HIGH_SPEED_MODE` which does not exist on ESP32-S/C variants.
- Solenoid coils are pulsed for `SOLENOID_PULSE_MS` (200 ms) and always de-energised afterwards — never hold a coil energised.
- Turnout wiring (`dirAddr`, `fwdPin`, `revPin` in SECTION_CONFIG and TURNOUT_CONFIG) is marked TODO and must be verified against physical hardware before use.
- GPIO 33 is used by section 2 enable pin — there is a noted conflict with a push button assignment. Resolve before connecting hardware.
