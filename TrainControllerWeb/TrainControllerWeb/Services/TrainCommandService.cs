namespace TrainControllerWeb.Services;

public sealed class TrainCommandService(RabbitMqService rabbit, FirmwareService firmware)
{
    // Tells a board to fetch and install a new image. The board reboots on
    // success; on failure it stays on its current firmware.
    public Task SendOtaUpdate(string device) =>
        rabbit.PublishAsync(
            $"train.ota.{device}",
            $"{{\"url\":\"{firmware.UrlFor(device)}\"}}");

    public Task SetTurnout(int id, string position) =>
        rabbit.PublishAsync(
            $"train.turnout.{id}",
            $"{{\"position\":\"{position}\"}}");

    // The 1/10 transposition that used to be corrected here now lives in the
    // firmware's SECTION_CONFIG, where the rest of the wiring is described. It
    // sat in this file only because a reflash was expensive; OTA made it a
    // button press, and one source of truth means anything publishing straight
    // to MQTT gets the same numbering the web page uses.
    public Task SetSection(int id, int speed, string direction) =>
        rabbit.PublishAsync(
            $"train.section.{id}",
            $"{{\"speed\":{speed},\"direction\":\"{direction}\"}}");

    public Task SetFan(int id, int speed) =>
        rabbit.PublishAsync(
            $"train.fan.{id}",
            $"{{\"speed\":{speed}}}");

    // Hands control back to the firmware's temperature thresholds.
    public Task SetFanAuto(int id) =>
        rabbit.PublishAsync(
            $"train.fan.{id}",
            "{\"mode\":\"auto\"}");

    public Task SetLed(int id, bool on) =>
        rabbit.PublishAsync(
            $"train.led.{id}",
            $"{{\"on\":{(on ? "true" : "false")}}}");
}
