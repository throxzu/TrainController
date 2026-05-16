# TrainControllerWeb — Blazor Web Controller

ASP.NET Core Blazor Server application for controlling the model railway. Publishes commands to RabbitMQ over AMQP, which the RabbitMQ MQTT plugin forwards to the ESP32.

## Requirements

- [.NET 10 SDK](https://dotnet.microsoft.com/download/dotnet/10.0)
- RabbitMQ 3.12+ with the MQTT plugin enabled

## RabbitMQ Setup

### Enable the MQTT plugin

The plugin is already enabled on this machine. If you reinstall RabbitMQ run:

```powershell
& "C:\Program Files\RabbitMQ Server\rabbitmq_server-3.12.12\sbin\rabbitmq-plugins.bat" enable rabbitmq_mqtt
```

### Create the ESP32 user {#rabbitmq-user}

The ESP32 firmware connects with username `esp32` and password `password123`. Create this user in RabbitMQ if it does not exist:

```powershell
$sbin = "C:\Program Files\RabbitMQ Server\rabbitmq_server-3.12.12\sbin"
& "$sbin\rabbitmqctl.bat" add_user esp32 password123
& "$sbin\rabbitmqctl.bat" set_permissions -p / esp32 ".*" ".*" ".*"
```

### Start RabbitMQ

```powershell
Start-Service RabbitMQ
```

Or open **Services** (`services.msc`) and start the RabbitMQ service manually.

## Configuration

RabbitMQ connection settings are in [`TrainControllerWeb/appsettings.json`](TrainControllerWeb/appsettings.json):

```json
"RabbitMQ": {
  "Host": "localhost",
  "Port": 5672,
  "Username": "guest",
  "Password": "guest"
}
```

The web app connects as the default `guest` user (AMQP). Only the ESP32 needs the `esp32` MQTT user.

## Run

```powershell
dotnet run --project TrainControllerWeb
```

Then open `http://localhost:5000` (or `https://localhost:5001`) in a browser.

## How It Works

The web app publishes to the built-in `amq.topic` exchange. RabbitMQ's MQTT plugin maps AMQP routing keys to MQTT topics by replacing `.` with `/`:

| AMQP routing key (C# publishes) | MQTT topic (ESP32 receives) |
|---|---|
| `train.section.5` | `train/section/5` |
| `train.turnout.2` | `train/turnout/2` |

No queues need to be created manually. RabbitMQ creates a temporary queue automatically when the ESP32 connects and subscribes to `train/#`.

## Project Structure

```
TrainControllerWeb/
├── Services/
│   ├── RabbitMqService.cs      AMQP connection and publish
│   └── TrainCommandService.cs  SetSection / SetTurnout helpers
├── Components/
│   ├── Pages/
│   │   └── Home.razor          Control panel UI
│   └── Layout/
│       └── MainLayout.razor
├── appsettings.json
└── Program.cs
```
