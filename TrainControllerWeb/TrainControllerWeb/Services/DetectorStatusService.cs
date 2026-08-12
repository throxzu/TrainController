using System.Text.Json;

namespace TrainControllerWeb.Services;

public sealed class DetectorStatusService
{
    private static readonly TimeSpan _timeout = TimeSpan.FromSeconds(30);

    private DateTime? _lastSeen;
    private readonly bool[] _occupied = new bool[14];

    // Raw last-known state of each individual Hall sensor, kept separately from
    // the latched occupancy above so the diagnostics table can show release
    // edges and sensors that have never reported at all.
    private readonly bool[]      _entryTriggered = new bool[14];
    private readonly bool[]      _exitTriggered  = new bool[14];
    private readonly DateTime?[] _entryUpdated   = new DateTime?[14];
    private readonly DateTime?[] _exitUpdated    = new DateTime?[14];

    public DateTime? LastSeen      => _lastSeen;
    public bool      IsAlive       => _lastSeen.HasValue && DateTime.UtcNow - _lastSeen.Value < _timeout;
    public uint      UptimeSeconds { get; private set; }

    public enum SectionState { Clear, Occupied }

    public SectionState GetState(int sectionIndex) =>
        _occupied[sectionIndex] ? SectionState.Occupied : SectionState.Clear;

    public readonly record struct SensorReading(
        int Index, int Section, bool IsEntry, byte Addr, byte Pin,
        bool Triggered, DateTime? LastUpdate)
    {
        public bool   HasData => LastUpdate.HasValue;
        public string Label   => $"S{Section} {(IsEntry ? "entry" : "exit")}";
    }

    // Wiring convention mirrored from the firmware's detect_config.h: sensors run
    // sequentially, entry then exit per section, filling each PCF8574 pin 0->7
    // before moving to the next I2C address.
    public static int  SensorIndex(int section, bool isEntry) => (section - 1) * 2 + (isEntry ? 0 : 1);
    public static byte SensorAddr(int index) => (byte)(0x20 + index / 8);
    public static byte SensorPin(int index)  => (byte)(index % 8);

    // All 28 sensors in physical wiring order (chip, then pin) — the order you
    // want when tracing a wiring fault.
    public IEnumerable<SensorReading> AllSensors()
    {
        for (int section = 1; section <= 14; section++)
        {
            foreach (var isEntry in new[] { true, false })
            {
                int i = SensorIndex(section, isEntry);
                yield return new SensorReading(
                    i, section, isEntry, SensorAddr(i), SensorPin(i),
                    isEntry ? _entryTriggered[section - 1] : _exitTriggered[section - 1],
                    isEntry ? _entryUpdated[section - 1]   : _exitUpdated[section - 1]);
            }
        }
    }

    // train/detector/temp — {"celsius":23.4,"fan":0,"auto":true}
    // or {"error":"no sensor","fan":0,"auto":true}
    public double?   TemperatureC     { get; private set; }
    public DateTime? TemperatureSeen  { get; private set; }
    public string?   TemperatureError { get; private set; }

    // Fan state as reported by the firmware — authoritative, since automatic
    // control can change it without the UI having asked for anything.
    public int  FanSpeed { get; private set; }
    public bool FanAuto  { get; private set; } = true;

    public void RecordTemperature(string json)
    {
        try
        {
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;

            if (root.TryGetProperty("celsius", out var c))
            {
                TemperatureC     = c.GetDouble();
                TemperatureError = null;
            }
            else if (root.TryGetProperty("error", out var e))
            {
                TemperatureC     = null;
                TemperatureError = e.GetString();
            }
            else return;

            if (root.TryGetProperty("fan",  out var f)) FanSpeed = f.GetInt32();
            if (root.TryGetProperty("auto", out var a)) FanAuto  = a.GetBoolean();

            TemperatureSeen = DateTime.UtcNow;
        }
        catch { return; }

        OnDetectionChanged?.Invoke();
    }

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
            if (root.TryGetProperty("uptime",      out var u)) UptimeSeconds = u.GetUInt32();
            if (root.TryGetProperty("version",     out var v)) Version       = v.GetString();
            if (root.TryGetProperty("built",       out var b)) Built         = b.GetString();
            if (root.TryGetProperty("heap",        out var h)) FreeHeap      = h.GetUInt32();
            if (root.TryGetProperty("wifi_drops",  out var d)) WifiDrops     = d.GetUInt32();
            if (root.TryGetProperty("wifi_reason", out var r)) WifiReason    = r.GetByte();
        }
        catch { }
    }

    // routingKey: "train.detection.N.entry" or "train.detection.N.exit"
    public event Action? OnDetectionChanged;

    public void RecordDetection(string routingKey, string json)
    {
        var parts = routingKey.Split('.');
        if (parts.Length < 4) return;
        if (!int.TryParse(parts[2], out var section) || section < 1 || section > 14) return;

        bool triggered = false;
        try
        {
            using var doc = JsonDocument.Parse(json);
            if (doc.RootElement.TryGetProperty("triggered", out var t))
                triggered = t.GetBoolean();
            else return;
        }
        catch { return; }

        // Record the raw sensor state first — including the release edge, which
        // the occupancy latch below deliberately ignores.
        if (parts[3] == "entry")
        {
            _entryTriggered[section - 1] = triggered;
            _entryUpdated[section - 1]   = DateTime.UtcNow;
            if (triggered) _occupied[section - 1] = true;
        }
        else if (parts[3] == "exit")
        {
            _exitTriggered[section - 1] = triggered;
            _exitUpdated[section - 1]   = DateTime.UtcNow;
            if (triggered) _occupied[section - 1] = false;
        }
        else return;

        OnDetectionChanged?.Invoke();
    }
}
