using System.Text.Json;

namespace TrainControllerWeb.Services;

public sealed class Esp32StatusService
{
    private static readonly TimeSpan _timeout = TimeSpan.FromSeconds(30);

    private DateTime? _lastSeen;

    public DateTime? LastSeen       => _lastSeen;
    public bool      IsAlive        => _lastSeen.HasValue && DateTime.UtcNow - _lastSeen.Value < _timeout;
    public uint      UptimeSeconds  { get; private set; }
    public uint      CmdsDispatched { get; private set; }
    public byte      I2cOkMask      { get; private set; }
    public uint      I2cErrors      { get; private set; }

    public int    I2cFoundCount => System.Numerics.BitOperations.PopCount(I2cOkMask);
    public byte[] I2cScanAddrs { get; private set; } = [];

    // Reported by the firmware so a wireless update can be confirmed without a
    // serial port — the version changes and the uptime resets.
    public string? Version { get; private set; }
    public string? Built   { get; private set; }

    // Link health. A board that keeps climbing WifiDrops is losing the AP and
    // recovering; one that goes quiet without the count moving died some other
    // way. WifiReason is the ESP-IDF disconnect code — 200 is beacon timeout,
    // 201 no AP found, 8 the AP dropping us on purpose.
    public uint FreeHeap   { get; private set; }
    public uint WifiDrops  { get; private set; }
    public byte WifiReason { get; private set; }

    public void RecordHeartbeat(string json)
    {
        _lastSeen = DateTime.UtcNow;
        try
        {
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;
            if (root.TryGetProperty("heap",        out var h)) FreeHeap   = h.GetUInt32();
            if (root.TryGetProperty("wifi_drops",  out var d)) WifiDrops  = d.GetUInt32();
            if (root.TryGetProperty("wifi_reason", out var r)) WifiReason = r.GetByte();
            if (root.TryGetProperty("uptime",   out var u)) UptimeSeconds  = u.GetUInt32();
            if (root.TryGetProperty("cmds",     out var c)) CmdsDispatched = c.GetUInt32();
            if (root.TryGetProperty("i2c_mask", out var m)) I2cOkMask      = m.GetByte();
            if (root.TryGetProperty("i2c_err",  out var e)) I2cErrors      = e.GetUInt32();
            if (root.TryGetProperty("scan",     out var s) && s.ValueKind == JsonValueKind.Array)
                I2cScanAddrs = s.EnumerateArray().Select(x => x.GetByte()).ToArray();
            if (root.TryGetProperty("version",  out var v)) Version = v.GetString();
            if (root.TryGetProperty("built",    out var b)) Built   = b.GetString();
        }
        catch { /* old firmware without fields — ignore */ }
    }
}
