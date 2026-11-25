using NeuPod.Platform.Linux;

namespace NeuPod.Y2Demo.Ui;

public enum ScreenId { Main, InputTest, DisplayTest, Backlight, System, About }

public interface IClock
{
    /// <summary>Monotonic milliseconds (same timebase as InputEvent.TimeMs).</summary>
    long NowMs { get; }
    DateTime LocalNow { get; }
}

/// <summary>Persists the one setting the demo keeps. Called rarely (eMMC).</summary>
public interface ISettingsStore
{
    void SaveBacklight(int value);
}

public sealed record InputLogEntry(string Name, string Action, long DeltaMs);

/// <summary>
/// All UI state and navigation, with no drawing and no I/O beyond the injected interfaces, so it runs under
/// host tests. The app loop feeds it input and timer ticks and redraws when <see cref="TakeRedraw"/> says so.
/// </summary>
public sealed class AppModel
{
    public const int InputLogSize = 8;
    public const long MenuHoldMs = 1000;
    public const int MenuRows = 11;
    public const int WheelStep = 8, VolumeStep = 16;
    public const long SystemRefreshMs = 1000, BackgroundRefreshMs = 30_000, WheelWindowMs = 1000, WheelRedrawMs = 200, HoldRedrawMs = 100;

    public static readonly (ScreenId Id, string Title)[] MenuItems =
    [
        (ScreenId.InputTest, "Input test"),
        (ScreenId.DisplayTest, "Display test"),
        (ScreenId.Backlight, "Backlight"),
        (ScreenId.System, "System"),
        (ScreenId.About, "About"),
    ];

    readonly IClock _clock;
    readonly ISysInfoSource _sys;
    readonly IBacklight _backlight;
    readonly ISettingsStore _store;

    bool _redraw = true;
    int _persistedBacklight;
    long _lastInfoMs, _lastWheelRedrawMs, _lastHoldRedrawMs;
    int _lastMinute = -1;

    // Input test state.
    readonly List<InputLogEntry> _log = [];
    readonly Dictionary<string, int> _counters = new(StringComparer.Ordinal);
    readonly Queue<long> _wheelTicks = new();
    long? _lastEventMs;
    long? _backDownMs;

    public AppModel(IClock clock, ISysInfoSource sys, IBacklight backlight, ISettingsStore store, string gitHash, string runtimeVersion)
    {
        _clock = clock; _sys = sys; _backlight = backlight; _store = store;
        GitHash = gitHash; RuntimeVersion = runtimeVersion;
        _persistedBacklight = backlight.Value;
        Menu = new ListState(MenuItems.Length, MenuRows);
        Info = new SysInfoSnapshot();
        RefreshInfo();
    }

    public string GitHash { get; }
    public string RuntimeVersion { get; }
    public ScreenId Screen { get; private set; } = ScreenId.Main;
    public ListState Menu { get; }
    public SysInfoSnapshot Info { get; private set; }
    public int BacklightValue => _backlight.Value;
    public IClock Clock => _clock;

    public IReadOnlyList<InputLogEntry> InputLog => _log;
    public IReadOnlyDictionary<string, int> Counters => _counters;
    public long InputHoldMs => _backDownMs is { } d ? Math.Max(0, _clock.NowMs - d) : 0;
    public bool BackHeld => _backDownMs is not null;

    // Display test state.
    public Pattern Pattern { get; private set; }
    public bool ShowOverlay { get; private set; } = true;
    public double? FlipFps { get; private set; }
    public bool BenchRequested { get; private set; }

    public string Title => Screen switch
    {
        ScreenId.Main => "neuPod Y2",
        ScreenId.InputTest => "Input test",
        ScreenId.DisplayTest => "Display test",
        ScreenId.Backlight => "Backlight",
        ScreenId.System => "System",
        _ => "About",
    };

    /// <summary>True once per invalidation; the loop redraws when it returns true.</summary>
    public bool TakeRedraw()
    {
        bool r = _redraw;
        _redraw = false;
        return r;
    }

    public bool TakeBenchRequest()
    {
        bool r = BenchRequested;
        BenchRequested = false;
        return r;
    }

    public void SetFlipFps(double fps)
    {
        FlipFps = fps;
        _redraw = true;
    }

    public int WheelTicksPerSecond(long nowMs)
    {
        PruneWheel(nowMs);
        return _wheelTicks.Count;
    }

    // ---- input ----

    public void Handle(InputEvent e)
    {
        if (e.Kind is InputKind.KeyPress or InputKind.KeyRepeat && e.Code is Keys.VolumeUp or Keys.VolumeDown)
            AdjustBacklight(e.Code == Keys.VolumeUp ? VolumeStep : -VolumeStep);

        switch (Screen)
        {
            case ScreenId.Main: HandleMain(e); break;
            case ScreenId.InputTest: HandleInputTest(e); break;
            case ScreenId.DisplayTest: HandleDisplayTest(e); break;
            case ScreenId.Backlight: HandleBacklight(e); break;
            default: if (e.Kind == InputKind.KeyPress && e.Code == Keys.Back) GoBack(); break;
        }
    }

    void HandleMain(InputEvent e)
    {
        switch (e.Kind)
        {
            case InputKind.WheelCw: if (Menu.Move(1)) _redraw = true; break;
            case InputKind.WheelCcw: if (Menu.Move(-1)) _redraw = true; break;
            case InputKind.KeyPress when e.Code == Keys.Enter: Open(MenuItems[Menu.Selected].Id); break;
        }
    }

    void HandleInputTest(InputEvent e)
    {
        if (e.Kind == InputKind.KeyRepeat) return; // the Menu hold would otherwise flood the log

        string name = e.IsWheel ? "WHEEL" : Keys.Name(e.Code);
        string action = e.Kind switch
        {
            InputKind.WheelCw => "cw",
            InputKind.WheelCcw => "ccw",
            InputKind.KeyPress => "press",
            _ => "release",
        };
        long delta = _lastEventMs is { } last ? e.TimeMs - last : 0;
        _lastEventMs = e.TimeMs;
        _log.Add(new InputLogEntry(name, action, delta));
        if (_log.Count > InputLogSize) _log.RemoveAt(0);

        if (e.Kind != InputKind.KeyRelease)
        {
            string key = e.IsWheel ? "WHEEL " + action.ToUpperInvariant() : name;
            _counters[key] = _counters.GetValueOrDefault(key) + 1;
        }
        if (e.IsWheel) _wheelTicks.Enqueue(e.TimeMs);

        // Menu is under test here, so a short press is only logged; holding it for MenuHoldMs leaves.
        if (e.Code == Keys.Back)
        {
            if (e.Kind == InputKind.KeyPress) _backDownMs = e.TimeMs;
            else if (e.Kind == InputKind.KeyRelease)
            {
                bool held = _backDownMs is { } d && e.TimeMs - d >= MenuHoldMs;
                _backDownMs = null;
                if (held) { GoBack(); return; }
            }
        }
        _redraw = true;
    }

    void HandleDisplayTest(InputEvent e)
    {
        int n = Enum.GetValues<Pattern>().Length;
        switch (e.Kind)
        {
            case InputKind.WheelCw: SetPattern(((int)Pattern + 1) % n); break;
            case InputKind.WheelCcw: SetPattern(((int)Pattern + n - 1) % n); break;
            case InputKind.KeyPress when e.Code == Keys.PlayPause: ShowOverlay = !ShowOverlay; _redraw = true; break;
            case InputKind.KeyPress when e.Code == Keys.Enter: BenchRequested = true; _redraw = true; break;
            case InputKind.KeyPress when e.Code == Keys.Back: GoBack(); break;
        }
    }

    void SetPattern(int index)
    {
        // Flip rate does not depend on the pattern, so cycling must not re-run the (blocking) measurement;
        // it runs once on entering the screen and again on Centre.
        Pattern = (Pattern)index;
        _redraw = true;
    }

    void HandleBacklight(InputEvent e)
    {
        switch (e.Kind)
        {
            case InputKind.WheelCw: AdjustBacklight(WheelStep); break;
            case InputKind.WheelCcw: AdjustBacklight(-WheelStep); break;
            case InputKind.KeyPress when e.Code == Keys.Back: GoBack(); break;
        }
    }

    void AdjustBacklight(int delta)
    {
        int v = Platform.Linux.Backlight.Clamp(_backlight.Value + delta);
        if (v == _backlight.Value) return;
        _backlight.Set(v);
        _redraw = true;
    }

    void Open(ScreenId id)
    {
        Screen = id;
        _redraw = true;
        switch (id)
        {
            case ScreenId.InputTest:
                _log.Clear(); _counters.Clear(); _wheelTicks.Clear();
                _lastEventMs = null; _backDownMs = null;
                break;
            case ScreenId.DisplayTest:
                FlipFps = null; BenchRequested = true;
                break;
            case ScreenId.System:
                RefreshInfo();
                break;
        }
    }

    void GoBack()
    {
        if (Screen == ScreenId.Main) return;
        if (Screen == ScreenId.Backlight) PersistBacklight();
        Screen = ScreenId.Main;
        _backDownMs = null;
        _redraw = true;
    }

    /// <summary>Write the backlight setting if it changed since it was last written. Also called on shutdown.</summary>
    public void PersistBacklight()
    {
        if (_backlight.Value == _persistedBacklight) return;
        _store.SaveBacklight(_backlight.Value);
        _persistedBacklight = _backlight.Value;
    }

    // ---- timers ----

    void RefreshInfo()
    {
        try { Info = _sys.Read(); }
        catch (Exception) { /* keep the previous snapshot; a failing source must not take the UI down */ }
        _lastInfoMs = _clock.NowMs;
    }

    void PruneWheel(long now)
    {
        while (_wheelTicks.Count > 0 && now - _wheelTicks.Peek() >= WheelWindowMs) _wheelTicks.Dequeue();
    }

    long InfoInterval => Screen == ScreenId.System ? SystemRefreshMs : BackgroundRefreshMs;

    /// <summary>Run due timers (hold-to-exit, 1 Hz System refresh, clock minute, wheel rate).</summary>
    public void Tick()
    {
        long now = _clock.NowMs;

        if (Screen == ScreenId.InputTest && _backDownMs is { } d)
        {
            if (now - d >= MenuHoldMs) { GoBack(); return; }
            if (now - _lastHoldRedrawMs >= HoldRedrawMs) { _lastHoldRedrawMs = now; _redraw = true; }
        }

        if (now - _lastInfoMs >= InfoInterval) { RefreshInfo(); _redraw = true; }

        int minute = _clock.LocalNow.Hour * 60 + _clock.LocalNow.Minute;
        if (minute != _lastMinute) { _lastMinute = minute; _redraw = true; }

        if (Screen == ScreenId.InputTest)
        {
            PruneWheel(now);
            if (_wheelTicks.Count > 0 && now - _lastWheelRedrawMs >= WheelRedrawMs) { _lastWheelRedrawMs = now; _redraw = true; }
        }
    }

    /// <summary>Milliseconds until Tick has something to do. Never infinite: the clock minute is always pending.</summary>
    public int NextWakeMs()
    {
        long now = _clock.NowMs;
        long wait = 60_000;

        var t = _clock.LocalNow;
        wait = Math.Min(wait, 60_000 - (t.Second * 1000 + t.Millisecond) + 20);
        wait = Math.Min(wait, _lastInfoMs + InfoInterval - now);

        if (Screen == ScreenId.InputTest)
        {
            if (_backDownMs is { } d) wait = Math.Min(wait, Math.Min(d + MenuHoldMs - now, _lastHoldRedrawMs + HoldRedrawMs - now));
            if (_wheelTicks.Count > 0) wait = Math.Min(wait, _lastWheelRedrawMs + WheelRedrawMs - now);
        }
        return (int)Math.Clamp(wait, 5, 60_000);
    }
}
