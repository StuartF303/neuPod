using System.Diagnostics;
using System.Globalization;
using NeuPod.Platform.Linux;
using NeuPod.Y2Demo.Ui;

namespace NeuPod.Y2Demo.Host;

public sealed class SystemClock : IClock
{
    public long NowMs => Environment.TickCount64; // monotonic ms; no multiply that could overflow
    public DateTime LocalNow => DateTime.Now;
}

/// <summary>Manually advanced clock for tests and PNG rendering.</summary>
public sealed class FakeClock(DateTime start) : IClock
{
    public long NowMs { get; set; }
    public DateTime Start { get; } = start;
    public DateTime LocalNow => Start.AddMilliseconds(NowMs);
    public void Advance(long ms) => NowMs += ms;
}

public sealed class MemoryBacklight(int initial = 128) : IBacklight
{
    public int Value { get; private set; } = Backlight.Clamp(initial);
    public void Set(int value) => Value = Backlight.Clamp(value);
}

public sealed class FakeSysInfo : ISysInfoSource
{
    public SysInfoSnapshot Snapshot { get; set; } = new()
    {
        Uptime = TimeSpan.FromSeconds(93_784),
        Load1 = 0.08, Load5 = 0.04, Load15 = 0.01,
        MemTotalKb = 994_204, MemAvailableKb = 880_000,
        OnlineCpus = 2,
        Kernel = "3.4.67",
        Ipv4 = "192.168.7.2",
        BatteryPercent = 72, BatteryMillivolts = 3_912, BatteryStatus = "Discharging",
        Jack = JackState.Headphones,
    };

    public int Reads { get; private set; }

    public SysInfoSnapshot Read()
    {
        Reads++;
        return Snapshot;
    }
}

public sealed class RecordingSettings : ISettingsStore
{
    public List<int> Saved { get; } = [];
    public void SaveBacklight(int value) => Saved.Add(value);
}

/// <summary>Tiny key=value conf file (/etc/neupod-demo.conf). Writes atomically and only when the value changed.</summary>
public sealed class ConfFile(string path) : ISettingsStore
{
    public string Path { get; } = path;

    public int? LoadBacklight()
    {
        try
        {
            foreach (var line in File.ReadAllLines(Path))
            {
                var p = line.Split('=', 2);
                if (p.Length == 2 && p[0].Trim() == "backlight"
                    && int.TryParse(p[1].Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out int v))
                    return Backlight.Clamp(v);
            }
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
        return null;
    }

    public void SaveBacklight(int value)
    {
        if (LoadBacklight() == value) return;
        string tmp = Path + ".tmp";
        try
        {
            using (var fs = new FileStream(tmp, FileMode.Create, FileAccess.Write))
            {
                var bytes = System.Text.Encoding.ASCII.GetBytes("backlight=" + value.ToString(CultureInfo.InvariantCulture) + "\n");
                fs.Write(bytes);
                fs.Flush(flushToDisk: true); // fsync before the rename so a power cut leaves the old or the new file, not a torn one
            }
            File.Move(tmp, Path, overwrite: true);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Console.Error.WriteLine($"could not save {Path}: {e.Message}");
        }
    }
}

/// <summary>
/// Blits the canvas to a frame target, round-robin over every page the target offers (mtkfb flips may complete
/// asynchronously and there is no vsync wait, so cycling all pages keeps us off the page being scanned out).
/// </summary>
public sealed class FramePresenter(IFrameTarget target)
{
    readonly PixelPacker _packer = new(target.Layout);
    int _next;

    public IFrameTarget Target { get; } = target;

    public void Show(Canvas c)
    {
        int page = Next();
        FrameBlit.Blit(c.Pixels, c.Width, c.Height, Target, page, _packer);
        if (Target.Present(page)) { _next = (page + 1) % Math.Max(1, Target.PageCount); return; }
        // The target refused the flip and dropped to a single page; redraw onto that page so the screen is not left stale.
        _next = 0;
        FrameBlit.Blit(c.Pixels, c.Width, c.Height, Target, 0, _packer);
        Target.Present(0);
    }

    public void Fill(uint rgb)
    {
        int page = Next();
        FrameBlit.Fill(Target, page, rgb, _packer);
        if (Target.Present(page)) { _next = (page + 1) % Math.Max(1, Target.PageCount); return; }
        _next = 0;
        FrameBlit.Fill(Target, 0, rgb, _packer);
        Target.Present(0);
    }

    int Next() => _next < Target.PageCount ? _next : 0;

    /// <summary>Full-frame blit+present rate over up to <paramref name="frames"/> frames or <paramref name="budgetMs"/>.</summary>
    public double MeasureFps(Canvas c, int frames = 30, int budgetMs = 1000)
    {
        var sw = Stopwatch.StartNew();
        int n = 0;
        while (n < frames && (n < 3 || sw.ElapsedMilliseconds < budgetMs)) { Show(c); n++; }
        return n * 1000.0 / Math.Max(1, sw.Elapsed.TotalMilliseconds);
    }
}
