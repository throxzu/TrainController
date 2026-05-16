# Model Railway Controller

A model railway control system built on an ESP32 microcontroller, controlled from a browser-based interface via RabbitMQ.

## System Architecture

```
Browser ──HTTP──► Blazor Web App ──AMQP:5672──► RabbitMQ ──MQTT:1883──► ESP32
                  TrainControllerWeb             (MQTT plugin)            TrainController
```

The web app publishes commands to RabbitMQ using AMQP. The RabbitMQ MQTT plugin bridges those messages to MQTT, which the ESP32 subscribes to over Wi-Fi.

## Layout

| Resource | Count |
|---|---|
| Track sections (independently powered) | 14 |
| Turnouts (solenoid-driven) | 7 |
| PCF8574 I/O expanders | 7 (addresses 0x20–0x26) |

## MQTT Topics

| Topic | Payload | Purpose |
|---|---|---|
| `train/section/{1–14}` | `{"speed":75,"direction":"forward"}` | Set section speed and direction |
| `train/turnout/{1–7}` | `{"position":"straight"}` | Throw a turnout |

Direction is `forward` or `reverse`. Speed is 0–100. Position is `straight` or `diverge`.

## Projects

| Folder | Description |
|---|---|
| [`esp32/TrainController/`](esp32/TrainController/README.md) | ESP32 firmware (ESP-IDF) |
| [`TrainControllerWeb/`](TrainControllerWeb/README.md) | Blazor web controller (.NET 10) |

## Quick Start

1. Start RabbitMQ on this PC
2. Flash the ESP32 firmware (see [ESP32 README](esp32/TrainController/README.md))
3. Run the web app: `dotnet run --project TrainControllerWeb\TrainControllerWeb`
4. Open `http://localhost:5000` in a browser
