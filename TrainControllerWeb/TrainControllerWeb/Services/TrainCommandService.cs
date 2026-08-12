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

    // Sections 1 and 10 are transposed between the UI numbering and the wiring:
    // the control labelled 1 drives the track the layout calls 10. Corrected
    // here rather than in the firmware so the fix needs no reflash — meaning
    // train/section/N still addresses the hardware as wired, and anything
    // publishing straight to MQTT bypasses this mapping.
    private static int MapSection(int id) => id switch
    {
        1  => 10,
        10 => 1,
        _  => id,
    };

    public Task SetSection(int id, int speed, string direction) =>
        rabbit.PublishAsync(
            $"train.section.{MapSection(id)}",
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
