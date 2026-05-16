# Skills & Commands

Reference for all commands needed to build, run, and manage this project.

## ESP32 Firmware

### Build

```powershell
# From c:\Train\esp32\TrainController\
idf.py build
```

### Flash & Monitor

```powershell
idf.py -p COM<N> flash monitor
# Press Ctrl-] to exit monitor
```

### Configure (Wi-Fi, MQTT broker URL)

```powershell
idf.py menuconfig
# Navigate to: Example Connection Configuration → Wi-Fi SSID/Password
# Navigate to: Broker URL → mqtt://<pc-ip>:1883
```

### Clean Build

```powershell
# Delete build\ folder manually — idf.py fullclean may fail if CMakeCache references old paths
Remove-Item -Recurse -Force build
idf.py build
```

### Invoke without sourcing the environment

```powershell
$python = "C:\Users\Darker\.espressif\python_env\idf5.1_py3.11_env\Scripts\python.exe"
$idf    = "C:\Users\Darker\esp\esp-idf\tools\idf.py"
& $python $idf build
```

---

## Web Controller

### Build

```powershell
# From C:\Train\TrainControllerWeb\
dotnet build TrainControllerWeb
```

### Run

```powershell
dotnet run --project TrainControllerWeb
# Open http://localhost:5000
```

### Add a NuGet package

```powershell
dotnet add TrainControllerWeb package <PackageName>
```

---

## RabbitMQ

### Start / Stop the service

```powershell
Start-Service RabbitMQ
Stop-Service RabbitMQ
```

### List plugins

```powershell
& "C:\Program Files\RabbitMQ Server\rabbitmq_server-3.12.12\sbin\rabbitmq-plugins.bat" list
```

### Enable MQTT plugin (already enabled — for reference)

```powershell
& "C:\Program Files\RabbitMQ Server\rabbitmq_server-3.12.12\sbin\rabbitmq-plugins.bat" enable rabbitmq_mqtt
```

### Manage users

```powershell
$sbin = "C:\Program Files\RabbitMQ Server\rabbitmq_server-3.12.12\sbin"

# Create the ESP32 MQTT user
& "$sbin\rabbitmqctl.bat" add_user esp32 password123
& "$sbin\rabbitmqctl.bat" set_permissions -p / esp32 ".*" ".*" ".*"

# List users
& "$sbin\rabbitmqctl.bat" list_users
```

### Open management UI

Navigate to `http://localhost:15672` (login: guest / guest).
The management plugin (`rabbitmq_management`) is already enabled.

---

## Git

```powershell
# ESP32 project — only after idf.py build succeeds
git -C "C:\Train\esp32\TrainController" add -p
git -C "C:\Train\esp32\TrainController" commit -m "message"
git -C "C:\Train\esp32\TrainController" push

# Web project — only after dotnet build succeeds
git -C "C:\Train\TrainControllerWeb" add -p
git -C "C:\Train\TrainControllerWeb" commit -m "message"
git -C "C:\Train\TrainControllerWeb" push
```
