using System.Text.Json;

namespace TrainControllerWeb.Services;

public sealed class DetectorStatusService
{
    private static readonly TimeSpan _timeout = TimeSpan.FromSeconds(30);

    private DateTime? _lastSeen;
    private readonly bool[] _entry = new bool[14];
    private readonly bool[] _exit  = new bool[14];

    public DateTime? LastSeen      => _lastSeen;
    public bool      IsAlive       => _lastSeen.HasValue && DateTime.UtcNow - _lastSeen.Value < _timeout;
    public uint      UptimeSeconds { get; private set; }

    public enum SectionState { Clear, Entering, Occupied, Leaving }

    public SectionState GetState(int sectionIndex)
    {
        bool e = _entry[sectionIndex], x = _exit[sectionIndex];
        return (e, x) switch
        {
            (true,  false) => SectionState.Entering,
            (true,  true)  => SectionState.Occupied,
            (false, true)  => SectionState.Leaving,
            _              => SectionState.Clear,
        };
    }

    public void RecordHeartbeat(string json)
    {
        _lastSeen = DateTime.UtcNow;
        try
        {
            using var doc = JsonDocument.Parse(json);
            if (doc.RootElement.TryGetProperty("uptime", out var u))
                UptimeSeconds = u.GetUInt32();
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

        if (parts[3] == "entry")     _entry[section - 1] = triggered;
        else if (parts[3] == "exit") _exit[section - 1]  = triggered;
        else return;

        OnDetectionChanged?.Invoke();
    }
}
