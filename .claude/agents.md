# Claude Agents

Two project-specific Claude agents are configured via `CLAUDE.md` files. Open the relevant project folder in Claude Code and the agent loads automatically.

## ESP32 Firmware Agent

**Location:** `esp32/TrainController/`
**Config:** [`esp32/TrainController/CLAUDE.md`](../esp32/TrainController/CLAUDE.md)

**Scope:**
- ESP-IDF C/C++ firmware
- FreeRTOS tasks, MQTT client, I2C, PCF8574, L298N motor control
- Component dependency management (ESP-IDF 5.x strict REQUIRES)
- Build, flash, and serial monitor

**Key rules this agent enforces:**
- Only push to git after a successful `idf.py build`
- Components must not depend on `main/` (circular dependency prevention)
- All REQUIRES must be declared explicitly in each component's `CMakeLists.txt`
- Never hold a solenoid coil energised beyond `SOLENOID_PULSE_MS`
- Target is ESP32 classic only — do not use APIs that don't exist on it

---

## Web Controller Agent

**Location:** `TrainControllerWeb/`
**Config:** [`TrainControllerWeb/CLAUDE.md`](../TrainControllerWeb/CLAUDE.md)

**Scope:**
- ASP.NET Core Blazor Server (.NET 10)
- RabbitMQ AMQP integration via `RabbitMQ.Client` 7.x
- Control panel UI (14 sections, 7 turnouts)

**Key rules this agent enforces:**
- Only push to git after `dotnet build` succeeds
- `RabbitMqService` and `TrainCommandService` must remain singletons — they hold a shared AMQP connection
- Publish channel is protected by `SemaphoreSlim` — do not remove thread safety
- AMQP routing keys use `.` as separator; RabbitMQ MQTT plugin maps these to MQTT `/` automatically

---

## How the Agents Interact

The two agents are independent but share a contract: the MQTT topic structure and payload format defined in the ESP32 firmware. Any change to topics or payload fields in one project must be reflected in the other.

| Defined in | Consumed by |
|---|---|
| `main/app_main.cpp` (topic patterns) | `Services/TrainCommandService.cs` (routing keys) |
| `train_config.h` (NUM_SECTIONS = 14, NUM_TURNOUTS = 7) | `Components/Pages/Home.razor` (loop counts) |
