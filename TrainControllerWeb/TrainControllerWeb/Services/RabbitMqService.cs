using RabbitMQ.Client;
using RabbitMQ.Client.Events;
using System.Text;

namespace TrainControllerWeb.Services;

public sealed class RabbitMqService : IAsyncDisposable
{
    private readonly ILogger<RabbitMqService> _logger;
    private readonly IConfiguration _config;
    private IConnection? _connection;
    private IChannel? _channel;
    private IChannel? _consumerChannel;
    private readonly SemaphoreSlim _publishLock = new(1, 1);

    public bool IsConnected => _connection?.IsOpen ?? false;

    public RabbitMqService(ILogger<RabbitMqService> logger, IConfiguration config)
    {
        _logger = logger;
        _config = config;
    }

    public async Task InitializeAsync()
    {
        var factory = new ConnectionFactory
        {
            HostName = _config["RabbitMQ:Host"] ?? "localhost",
            Port     = _config.GetValue<int>("RabbitMQ:Port", 5672),
            UserName = _config["RabbitMQ:Username"] ?? "guest",
            Password = _config["RabbitMQ:Password"] ?? "guest",
        };

        try
        {
            _connection     = await factory.CreateConnectionAsync();
            _channel        = await _connection.CreateChannelAsync();
            _consumerChannel = await _connection.CreateChannelAsync();
            _logger.LogInformation("RabbitMQ connected to {Host}", factory.HostName);
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to connect to RabbitMQ at {Host}:{Port}", factory.HostName, factory.Port);
        }
    }

    public async Task PublishAsync(string routingKey, string json)
    {
        if (_channel is null)
        {
            _logger.LogWarning("Publish skipped — not connected (key={Key})", routingKey);
            return;
        }

        var body = Encoding.UTF8.GetBytes(json);

        await _publishLock.WaitAsync();
        try
        {
            await _channel.BasicPublishAsync(
                exchange:   "amq.topic",
                routingKey: routingKey,
                body:       body);
        }
        finally
        {
            _publishLock.Release();
        }
    }

    public async Task StartConsumerAsync(string routingKey, Func<string, Task> handler)
    {
        if (_consumerChannel is null) return;

        var q = await _consumerChannel.QueueDeclareAsync(
            queue: "", durable: false, exclusive: true, autoDelete: true);

        await _consumerChannel.QueueBindAsync(
            queue: q.QueueName, exchange: "amq.topic", routingKey: routingKey);

        var consumer = new AsyncEventingBasicConsumer(_consumerChannel);
        consumer.ReceivedAsync += async (_, ea) =>
        {
            var body = Encoding.UTF8.GetString(ea.Body.Span);
            await handler(body);
        };

        await _consumerChannel.BasicConsumeAsync(
            queue: q.QueueName, autoAck: true, consumer: consumer);

        _logger.LogInformation("Consuming from amq.topic with key '{Key}'", routingKey);
    }

    public async ValueTask DisposeAsync()
    {
        _publishLock.Dispose();
        if (_consumerChannel is not null) await _consumerChannel.DisposeAsync();
        if (_channel is not null)         await _channel.DisposeAsync();
        if (_connection is not null)      await _connection.DisposeAsync();
    }
}
