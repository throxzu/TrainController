namespace TrainControllerWeb.Services;

public sealed class TrainCommandService(RabbitMqService rabbit)
{
    public Task SetTurnout(int id, string position) =>
        rabbit.PublishAsync(
            $"train.turnout.{id}",
            $"{{\"position\":\"{position}\"}}");

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
