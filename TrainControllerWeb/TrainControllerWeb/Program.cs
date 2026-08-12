using TrainControllerWeb.Components;
using TrainControllerWeb.Services;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddRazorComponents()
    .AddInteractiveServerComponents();

builder.Services.AddSingleton<RabbitMqService>();
builder.Services.AddSingleton<TrainCommandService>();
builder.Services.AddSingleton<Esp32StatusService>();
builder.Services.AddSingleton<DetectorStatusService>();
builder.Services.AddSingleton<FirmwareService>();

var app = builder.Build();

if (!app.Environment.IsDevelopment())
{
    app.UseExceptionHandler("/Error", createScopeForErrors: true);
    app.UseHsts();
}

app.UseStatusCodePagesWithReExecute("/not-found", createScopeForStatusCodePages: true);

// The ESP32 fetches firmware over plain HTTP and cannot follow a redirect to
// HTTPS, so /firmware is deliberately exempt from the redirect.
app.UseWhen(
    ctx => !ctx.Request.Path.StartsWithSegments("/firmware"),
    branch => branch.UseHttpsRedirection());

app.UseAntiforgery();
app.MapStaticAssets();
app.MapRazorComponents<App>()
    .AddInteractiveServerRenderMode();

// Serves firmware images to the ESP32s during an OTA update.
//
// Not MapStaticAssets: that resolves from a build-time manifest, so a .bin
// rebuilt afterwards would never be picked up. This reads the file on each
// request, which is exactly what "build then flash" needs.
// HEAD as well as GET: the ESP32 only issues GET, but HEAD makes the endpoint
// probeable with curl -I without pulling a megabyte down.
app.MapMethods("/firmware/{device}", ["GET", "HEAD"], (string device, FirmwareService firmware, ILoggerFactory lf) =>
{
    var path = firmware.PathFor(device);
    if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
    {
        lf.CreateLogger("Firmware").LogWarning(
            "firmware requested for unknown or unbuilt device '{Device}' (path: {Path})",
            device, path ?? "<unconfigured>");
        return Results.NotFound();
    }

    lf.CreateLogger("Firmware").LogInformation("serving {Device} firmware from {Path}", device, path);
    return Results.File(path, "application/octet-stream");
});

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
