namespace TrainControllerWeb.Services;

/// <summary>
/// Locates the firmware images the ESP32s download during an OTA update.
///
/// Images are read straight from each ESP-IDF build folder rather than being
/// copied into wwwroot, so whatever was last built is what gets flashed — no
/// staging step to forget.
/// </summary>
public sealed class FirmwareService(IConfiguration config)
{
    public readonly record struct ImageInfo(bool Exists, long Bytes, DateTime? BuiltLocal)
    {
        public string SizeText => Exists ? $"{Bytes / 1024.0 / 1024.0:0.00} MB" : "—";
    }

    public string? PathFor(string device) => config[$"Firmware:Images:{device}"];

    /// <summary>Host:port handed to the board — must be reachable from the LAN.</summary>
    public string AdvertisedHost => config["Firmware:AdvertisedHost"] ?? "localhost:5264";

    public string UrlFor(string device) => $"http://{AdvertisedHost}/firmware/{device}";

    public ImageInfo Describe(string device)
    {
        var path = PathFor(device);
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
            return new ImageInfo(false, 0, null);

        var info = new FileInfo(path);
        return new ImageInfo(true, info.Length, info.LastWriteTime);
    }
}
