# TrainControllerWeb — Blazor Web Controller

ASP.NET Core Blazor Server (.NET 10) web app for controlling the model railway. Publishes commands to RabbitMQ via AMQP; the RabbitMQ MQTT plugin forwards them to the ESP32.

## Build & Run

```powershell
dotnet build TrainControllerWeb
dotnet run --project TrainControllerWeb
```

App is available at `http://localhost:5000` / `https://localhost:5001`.

## Project Structure

```
TrainControllerWeb/
  Services/
    RabbitMqService.cs      Singleton — AMQP connection, thread-safe BasicPublishAsync
    TrainCommandService.cs  Singleton — SetSection(id, speed, direction) / SetTurnout(id, position)
  Components/
    Pages/Home.razor        Control panel — 14 section cards, 7 turnout cards
    Layout/MainLayout.razor
    _Imports.razor          Includes TrainControllerWeb.Services namespace
  appsettings.json          RabbitMQ connection config
  Program.cs                Registers singletons, calls rabbit.InitializeAsync() at startup
```

## RabbitMQ

- Running locally on this PC (RabbitMQ 3.12.12, installed at `C:\Program Files\RabbitMQ Server\`)
- MQTT plugin is already enabled
- The web app connects as AMQP user `guest`/`guest` on port 5672
- The ESP32 connects as MQTT user `esp32`/`password123` on port 1883

To start RabbitMQ: `Start-Service RabbitMQ`

Config in `appsettings.json`:
```json
"RabbitMQ": { "Host": "localhost", "Port": 5672, "Username": "guest", "Password": "guest" }
```

## AMQP → MQTT Routing

The app publishes to the built-in `amq.topic` exchange. RabbitMQ maps routing key `.` separators to MQTT `/` separators automatically.

| C# publishes (routing key) | ESP32 receives (MQTT topic) |
|---|---|
| `train.section.5` | `train/section/5` |
| `train.turnout.2` | `train/turnout/2` |

No queues need to be created manually — RabbitMQ creates a temporary queue when the ESP32 subscribes.

## Layout

- 14 sections: speed 0–100, direction `forward`/`reverse`
- 7 turnouts: position `straight`/`diverge`

These match `NUM_SECTIONS` and `NUM_TURNOUTS` in the ESP32 firmware's `train_config.h`.

## Key Constraints

- `RabbitMqService` is a **singleton** — one AMQP connection for the lifetime of the app. The publish channel is protected by a `SemaphoreSlim` for thread safety across concurrent Blazor Server circuits.
- If RabbitMQ is not running when the app starts, `IsConnected` will be false and publishes are silently dropped. The UI shows a red "Disconnected" badge.
- Do not add scoped or transient lifetimes to `RabbitMqService` or `TrainCommandService` — they hold a shared connection.
