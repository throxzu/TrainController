# Claude Agents

Three project-specific Claude agents are configured via `CLAUDE.md` files. Open the relevant project folder in Claude Code and the agent loads automatically.

## ESP32 Controller Agent

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

## ESP32 Detector Agent

**Location:** `esp32/TrainDetector/`
**Config:** [`esp32/TrainDetector/CLAUDE.md`](../esp32/TrainDetector/CLAUDE.md)

**Scope:**
- ESP-IDF C/C++ firmware
- Reed switch detection via PCF8574 I/O expander inputs
- MQTT publishing of occupancy state (`train/detection/{n}`)
- Component dependency management (ESP-IDF 5.x strict REQUIRES)
- Build, flash, and serial monitor

**Key rules this agent enforces:**
- Only push to git after a successful `idf.py build`
- Components must not depend on `main/` (circular dependency prevention)
- All REQUIRES must be declared explicitly in each component's `CMakeLists.txt`
- Target is ESP32 classic only — do not use APIs that don't exist on it
- PCF8574 pins must be written 0xFF before reading (input mode)

---

## Web Controller Agent

**Location:** `TrainControllerWeb/`
**Config:** [`TrainControllerWeb/CLAUDE.md`](../TrainControllerWeb/CLAUDE.md)

**Scope:**
- ASP.NET Core Blazor Server (.NET 10)
- RabbitMQ AMQP integration via `RabbitMQ.Client` 7.x
- Control panel UI (14 sections, 6 turnouts)

**Key rules this agent enforces:**
- Only push to git after `dotnet build` succeeds
- `RabbitMqService` and `TrainCommandService` must remain singletons — they hold a shared AMQP connection
- Publish channel is protected by `SemaphoreSlim` — do not remove thread safety
- AMQP routing keys use `.` as separator; RabbitMQ MQTT plugin maps these to MQTT `/` automatically

---

## How the Agents Interact

The agents share a contract: the MQTT topic structure and payload format.
Any change to topics or payload fields must be reflected in all consumers.

| Defined in | Consumed by |
|---|---|
| `TrainController/main/app_main.cpp` (topic patterns) | `TrainControllerWeb/Services/TrainCommandService.cs` |
| `TrainDetector/main/app_main.cpp` (detection topics) | `TrainControllerWeb` (TODO: subscribe to train/detection/#) |
| `detect_config.h` (NUM_SECTIONS = 14) | `TrainDetector/main/app_main.cpp` |
| `train_config.h` (NUM_SECTIONS = 14, NUM_TURNOUTS = 6) | `Home.razor` (loop counts) |
