# Project Context

## What This Is

A model railway controller built on an ESP32. The ESP32 manages physical track sections and turnouts. A browser-based web app sends commands to the ESP32 via a RabbitMQ message broker running on a local PC.

## Architecture Decisions

### Why RabbitMQ instead of a plain MQTT broker (e.g. Mosquitto)?

RabbitMQ with the MQTT plugin gives a single broker that speaks both AMQP and MQTT. The C# web app connects via AMQP (richer protocol, better .NET client library), while the ESP32 uses MQTT (lightweight, native ESP-IDF support). RabbitMQ bridges the two transparently via the `amq.topic` exchange.

### Why Blazor Server instead of a desktop app?

The control panel needs to be accessible from any device on the local network (PC, tablet, phone). Blazor Server provides real-time UI updates via SignalR without requiring a separate API layer or JavaScript framework.

### Why copy `protocol_examples_common` locally instead of referencing `$ENV{IDF_PATH}`?

The `$ENV{IDF_PATH}` reference is fragile — it breaks when the project is opened in PlatformIO or on a machine with a different ESP-IDF install path. Copying the component into `components/` makes the build self-contained.

### Why `esp_err_to_name()` instead of `GeneralUtils::errorToString()`?

The `I2C` component originally used `GeneralUtils.h` from `main/`, which created a circular dependency (component → main). `esp_err_to_name()` is the ESP-IDF built-in equivalent and removes the dependency entirely.

## Current State

| Area | Status |
|---|---|
| ESP32 firmware | Builds cleanly. Hardware not yet fully wired — turnout pin assignments are provisional |
| Web controller | Builds and runs. Connects to RabbitMQ; publishes commands to ESP32 |
| RabbitMQ | Installed locally, MQTT plugin enabled, management UI available |
| Documentation | README for each project + top-level overview |
| GitHub | ESP32 repo at https://github.com/throxzu/TrainController |

## Known Issues

| Issue | Location | Notes |
|---|---|---|
| GPIO33 conflict | `train_config.h`, `app_main.cpp` | Section 2 enable pin and push button both use GPIO33 — must reassign one |
| Turnout wiring unverified | `train_config.h` | All `dirAddr`/`straightPin`/`divergePin` values marked TODO |
| Hardcoded MQTT credentials | `app_main.cpp` | `esp32` / `password123` — fine for home use, move to `sdkconfig` if needed |
| No reconnect logic | `RabbitMqService.cs` | If RabbitMQ restarts, the web app must be restarted too |

## Future Considerations

- Add status feedback from ESP32 (publish section speed / turnout position back to a `train/status/#` topic)
- Subscribe in the Blazor app and reflect live ESP32 state in the UI
- Add per-section emergency stop button
- Consider persisting turnout state across ESP32 reboots (NVS flash)
- Move Wi-Fi and MQTT credentials out of hardcoded values and into `sdkconfig`
