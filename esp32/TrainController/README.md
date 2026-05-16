# TrainController — ESP32 Firmware

ESP-IDF firmware for the ESP32-based model railway controller. Connects to a RabbitMQ broker over Wi-Fi and controls track sections and turnouts via MQTT commands.

## Hardware

| Component | Details |
|---|---|
| MCU | ESP32 (classic, dual-core) |
| I2C SDA | GPIO 21 |
| I2C SCL | GPIO 22 |
| I/O expanders | 7× PCF8574 at addresses 0x20–0x26 |
| Motor drivers | L298N per track section (14 sections) |
| Section PWM | LEDC — 10 kHz, 10-bit resolution |
| Turnouts | Solenoid coils, pulsed for 200 ms then de-energised |

Section enable pins and PCF8574 direction pins are defined in [`components/train_config/train_config.h`](components/train_config/train_config.h). Turnout wiring is in the same file and marked `TODO: verify` until physically confirmed.

## Software Requirements

- ESP-IDF v5.1.2
- Python 3.11 (via the ESP-IDF toolchain installer)

## Configuration

Run menuconfig to set Wi-Fi credentials and the MQTT broker address:

```
idf.py menuconfig
```

- **Wi-Fi** → `Example Connection Configuration` → SSID and Password
- **MQTT** → `Broker URL` → `mqtt://<your-pc-ip>:1883`

The firmware authenticates to the broker with username `esp32` and password `password123` (hardcoded in `main/app_main.cpp`). Make sure the matching RabbitMQ user exists — see the [web controller setup](../../../TrainControllerWeb/README.md#rabbitmq-user).

## Build

```powershell
idf.py build
```

## Flash

```powershell
idf.py -p COM<N> flash
```

Replace `COM<N>` with the COM port of your ESP32. Add `monitor` to also open the serial console:

```powershell
idf.py -p COM<N> flash monitor
```

Press `Ctrl-]` to exit the monitor.

## Component Structure

```
components/
├── I2C/                        I2C master driver wrapper
├── PCF8574/                    PCF8574 I/O expander driver
├── motor_l298n/                L298N PWM speed control (FreeRTOS task)
├── train_config/               Pin assignments and layout constants (header-only)
└── protocol_examples_common/   Wi-Fi connection helper (copied from ESP-IDF examples)
main/
├── app_main.cpp                Entry point, MQTT client, message dispatch
├── turnout_control.cpp/h       Turnout FreeRTOS task
└── GeneralUtils.cpp/h          Utility helpers
```

## MQTT Topics

The firmware subscribes to `train/#` and dispatches on topic pattern:

| Topic | Payload | Notes |
|---|---|---|
| `train/section/{id}` | `{"speed":75,"direction":"forward"}` | Controls L298N enable (PWM) and direction via PCF8574 |
| `train/turnout/{id}` | `{"position":"straight"}` | Pulses solenoid coil for 200 ms then de-energises |

Section IDs are 1–14, turnout IDs are 1–7. Direction is `forward` or `reverse`. Speed is 0–100.
