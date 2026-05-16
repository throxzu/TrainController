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
}
