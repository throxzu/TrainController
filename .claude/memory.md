# Project Memory

Key facts to carry into any session on this project. Verify against current code before acting on anything marked with a version or path.

## Hardware

- **MCU:** ESP32 classic (dual-core) — not S2/S3/C3; uses `LEDC_HIGH_SPEED_MODE` which only exists on the original ESP32
- **I2C:** SDA GPIO21, SCL GPIO22
- **PCF8574 expanders:** 7 chips at addresses 0x20–0x26 (56 control pins total)
- **Track sections:** 14, each driven by an L298N motor driver
- **Turnouts:** 7, solenoid-driven via PCF8574; coil pulsed 200 ms then de-energised
- **GPIO33 conflict:** used by both section 2 enable pin and a push button — must be resolved before connecting hardware

## MQTT Topics

| Topic | Payload |
|---|---|
| `train/section/{1–14}` | `{"speed":0–100,"direction":"forward"\|"reverse"}` |
| `train/turnout/{1–7}` | `{"position":"straight"\|"diverge"}` |

ESP32 subscribes to `train/#`, QoS 0, clean session.

## Credentials & Ports

| Service | User | Password | Port |
|---|---|---|---|
| RabbitMQ AMQP (C# app) | guest | guest | 5672 |
| RabbitMQ MQTT (ESP32) | esp32 | password123 | 1883 |

Credentials are hardcoded in `main/app_main.cpp`. The `esp32` RabbitMQ user must have full permissions on vhost `/`.

## Toolchain Paths (this PC)

| Tool | Path |
|---|---|
| ESP-IDF | `C:\Users\Darker\esp\esp-idf` (v5.1.2) |
| ESP-IDF Python | `C:\Users\Darker\.espressif\python_env\idf5.1_py3.11_env\Scripts\python.exe` |
| RabbitMQ sbin | `C:\Program Files\RabbitMQ Server\rabbitmq_server-3.12.12\sbin\` |
| .NET SDK | 10.0.300 |

## Known Issues / TODOs

- Turnout wiring (`dirAddr`, `fwdPin`, `revPin`) in `train_config.h` is provisional — marked `TODO: verify` throughout
- GPIO33 conflict between section 2 enable pin and push button
- MQTT credentials are hardcoded — should move to `sdkconfig` for production

## Git Rules

- ESP32 project: only push after `idf.py build` succeeds
- Web project: only push after `dotnet build` succeeds
