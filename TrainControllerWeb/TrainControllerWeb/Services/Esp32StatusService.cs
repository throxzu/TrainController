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

    public void RecordHeartbeat(string json)
    {
        _lastSeen = DateTime.UtcNow;
        try
        {
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;
            if (root.TryGetProperty("uptime",   out var u)) UptimeSeconds  = u.GetUInt32();
            if (root.TryGetProperty("cmds",     out var c)) CmdsDispatched = c.GetUInt32();
            if (root.TryGetProperty("i2c_mask", out var m)) I2cOkMask      = m.GetByte();
            if (root.TryGetProperty("i2c_err",  out var e)) I2cErrors      = e.GetUInt32();
            if (root.TryGetProperty("scan",     out var s) && s.ValueKind == JsonValueKind.Array)
                I2cScanAddrs = s.EnumerateArray().Select(x => x.GetByte()).ToArray();
        }
        catch { /* old firmware without fields — ignore */ }
    }
}
