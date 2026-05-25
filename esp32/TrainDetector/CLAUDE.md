# TrainDetector — ESP32 Firmware

ESP-IDF v5.1.2 project for an ESP32 train presence detector. C/C++, FreeRTOS, MQTT over Wi-Fi.
Detects trains via reed switches wired to PCF8574 I/O expander inputs and publishes occupancy over MQTT.

## Build & Flash

```powershell
$idf_python = "C:\Users\Darker\.espressif\python_env\idf5.1_py3.11_env\Scripts\python.exe"
$idf_path   = "C:\Users\Darker\esp\esp-idf"
$env:IDF_PATH = $idf_path
$env:PATH = "C:\Users\Darker\.espressif\tools\cmake\3.24.0\bin;C:\Users\Darker\.espressif\tools\ninja\1.10.2\;C:\Users\Darker\.espressif\tools\xtensa-esp32-elf\esp-2022r1-11.2.0\xtensa-esp32-elf\bin;$env:PATH"
Set-Location "C:\Train\esp32\TrainDetector"
& $idf_python "$idf_path\tools\idf.py" build
& $idf_python "$idf_path\tools\idf.py" -p COM<N> flash monitor
```

## Project Structure

```
main/
  app_main.cpp        Wi-Fi, MQTT client, detection task, heartbeat task

components/
  I2C/                I2C master wrapper (shared with TrainController)
  PCF8574/            PCF8574 I/O expander driver (shared with TrainController)
  detect_config/      Header-only — section count, PCF8574 addresses, poll interval
  protocol_examples_common/  Wi-Fi connection helper
```

## Detection Principle

Reed switches connect PCF8574 input pins to GND. The PCF8574's quasi-bidirectional outputs
hold pins HIGH when idle. A train passing closes the reed switch, pulling the pin LOW.

- LOW bit on PCF8574 read = **occupied**
- HIGH bit = **clear**

Each PCF8574 is initialised with `write(0xFF)` to put all pins into input mode.

## Configuration (`detect_config.h`)

| Constant | Value | Meaning |
|---|---|---|
| `NUM_SECTIONS` | 14 | Track sections to monitor |
| `NUM_DETECT_EXPANDERS` | 2 | PCF8574 chips (TODO: update when hardware arrives) |
| `DETECT_EXPANDER_ADDR[]` | 0x20, 0x21 | I2C addresses (TODO: verify with hardware) |
| `DETECT_POLL_MS` | 100 | Reed switch poll interval |
| `DETECT_I2C_SDA` | GPIO21 | I2C data pin |
| `DETECT_I2C_SCL` | GPIO22 | I2C clock pin |

## MQTT Topics

| Topic | Payload | Notes |
|---|---|---|
| `train/detection/{1–14}` | `{"occupied":true\|false}` | Published on state change, retained |
| `train/detector/status` | `{"status":"alive","uptime":N}` | Heartbeat every 10 s |

MQTT credentials: username `esp32`, password `password123`, client ID `ESP32-detector`.
Broker URL configured via `CONFIG_BROKER_URL` in sdkconfig (`idf.py menuconfig` → Example Configuration).

## Component Dependency Rules

- Components **cannot** depend on `main/`. All inter-component dependencies must be declared in `idf_component_register(... REQUIRES ...)`.
- ESP-IDF 5.x strictly enforces REQUIRES — missing entries cause build failures.
- Target is **ESP32 classic only** — do not use APIs that don't exist on ESP32-S/C variants.

## Git

Only push to git after a successful `idf.py build`. Never push a build that fails to compile.

## TODO

- Verify PCF8574 I2C addresses once hardware is wired (update `detect_config.h`)
- Confirm section-to-pin mapping on each expander
- Run `idf.py menuconfig` to set Wi-Fi SSID/password and broker URL before first build
- Consider debounce if reed switches show chatter
