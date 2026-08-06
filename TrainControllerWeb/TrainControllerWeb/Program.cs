using TrainControllerWeb.Components;
using TrainControllerWeb.Services;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddRazorComponents()
    .AddInteractiveServerComponents();

builder.Services.AddSingleton<RabbitMqService>();
builder.Services.AddSingleton<TrainCommandService>();
builder.Services.AddSingleton<Esp32StatusService>();
builder.Services.AddSingleton<DetectorStatusService>();

var app = builder.Build();

if (!app.Environment.IsDevelopment())
{
    app.UseExceptionHandler("/Error", createScopeForErrors: true);
    app.UseHsts();
}

app.UseStatusCodePagesWithReExecute("/not-found", createScopeForStatusCodePages: true);
app.UseHttpsRedirection();
app.UseAntiforgery();
app.MapStaticAssets();
app.MapRazorComponents<App>()
    .AddInteractiveServerRenderMode();

// Connect to RabbitMQ before accepting requests
var rabbit = app.Services.GetRequiredService<RabbitMqService>();
await rabbit.InitializeAsync();

var esp32Status = app.Services.GetRequiredService<Esp32StatusService>();
await rabbit.StartConsumerAsync("train.status", body =>
{
    esp32Status.RecordHeartbeat(body);
    return Task.CompletedTask;
});

var detectorStatus = app.Services.GetRequiredService<DetectorStatusService>();
await rabbit.StartConsumerAsync("train.detector.status", body =>
{
    detectorStatus.RecordHeartbeat(body);
    return Task.CompletedTask;
});
await rabbit.StartConsumerAsync("train.detector.temp", body =>
{
    detectorStatus.RecordTemperature(body);
    return Task.CompletedTask;
});
await rabbit.StartConsumerAsync("train.detection.#", (routingKey, body) =>
{
    detectorStatus.RecordDetection(routingKey, body);
    return Task.CompletedTask;
});

app.Run();
