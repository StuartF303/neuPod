using System.Diagnostics;
using System.Threading.Channels;

namespace NeuPod.Platform.Linux;

/// <summary>Finds /dev/input/eventN by the device name in sysfs, never by number.</summary>
public static class InputDeviceFinder
{
    public const string DefaultSysClassInput = "/sys/class/input";
    public const string DefaultDevInput = "/dev/input";

    /// <summary>Returns the event node path for each requested name that exists (missing names are skipped).</summary>
    public static Dictionary<string, string> Find(IEnumerable<string> names, string sysClassInput = DefaultSysClassInput, string devInput = DefaultDevInput)
    {
        var wanted = new HashSet<string>(names, StringComparer.Ordinal);
        var found = new Dictionary<string, string>(StringComparer.Ordinal);
        if (!Directory.Exists(sysClassInput)) return found;
        foreach (var dir in Directory.GetDirectories(sysClassInput, "event*").OrderBy(d => d, StringComparer.Ordinal))
        {
            string nameFile = Path.Combine(dir, "device", "name");
            if (!File.Exists(nameFile)) continue;
            string name = File.ReadAllText(nameFile).Trim();
            if (wanted.Contains(name) && !found.ContainsKey(name))
                found[name] = Path.Combine(devInput, Path.GetFileName(dir));
        }
        return found;
    }
}

/// <summary>
/// Reads evdev devices on background threads (blocking read with a poll timeout so Dispose is prompt) and
/// publishes typed, debounced events on <see cref="Events"/>.
/// </summary>
public sealed unsafe class EvdevInput : IDisposable
{
    public static readonly string[] DefaultDeviceNames = ["mtk-tpd", "mtk-kpd"];

    readonly Channel<InputEvent> _channel = Channel.CreateUnbounded<InputEvent>(new UnboundedChannelOptions { SingleReader = true });
    readonly List<Thread> _threads = [];
    readonly CancellationTokenSource _cts = new();
    readonly RingDebounce _debounce;
    readonly Func<long> _clock;
    readonly object _gate = new();

    public EvdevInput(IEnumerable<string> devicePaths, RingDebounce? debounce = null, Func<long>? clock = null)
    {
        _debounce = debounce ?? new RingDebounce();
        _clock = clock ?? (() => Stopwatch.GetTimestamp() * 1000 / Stopwatch.Frequency);
        foreach (var path in devicePaths)
        {
            int fd = Native.open(path, Native.O_RDONLY | Native.O_CLOEXEC);
            if (fd < 0)
                throw new IOException($"cannot open {path} (errno {System.Runtime.InteropServices.Marshal.GetLastWin32Error()})");
            var t = new Thread(() => ReadLoop(fd, path)) { IsBackground = true, Name = "evdev " + path };
            _threads.Add(t);
        }
    }

    /// <summary>Discover the default Y2 devices by name and open them. Throws if none are found.</summary>
    public static EvdevInput OpenDefault(string sysClassInput = InputDeviceFinder.DefaultSysClassInput, string devInput = InputDeviceFinder.DefaultDevInput)
    {
        var found = InputDeviceFinder.Find(DefaultDeviceNames, sysClassInput, devInput);
        if (found.Count == 0) throw new IOException("no input devices found (" + string.Join(", ", DefaultDeviceNames) + ")");
        return new EvdevInput(found.Values);
    }

    public ChannelReader<InputEvent> Events => _channel.Reader;

    public void Start()
    {
        foreach (var t in _threads) t.Start();
    }

    void ReadLoop(int fd, string path)
    {
        var decoder = new InputEventDecoder();
        var raw = new List<RawInputEvent>();
        var buf = new byte[RawInputEvent.Size * 32];
        try
        {
            while (!_cts.IsCancellationRequested)
            {
                var pfd = new Native.PollFd { fd = fd, events = Native.POLLIN };
                int pr = Native.poll(&pfd, 1, 250);
                if (pr <= 0) continue; // timeout or EINTR; re-check cancellation
                nint n;
                fixed (byte* p = buf) n = Native.read(fd, p, (nuint)buf.Length);
                if (n <= 0) break; // device gone
                raw.Clear();
                decoder.Feed(buf.AsSpan(0, (int)n), raw);
                long now = _clock();
                foreach (var r in raw)
                {
                    var e = InputMapper.Map(r, now);
                    if (e is null) continue;
                    bool ok;
                    lock (_gate) ok = _debounce.Accept(e.Value);
                    if (ok) _channel.Writer.TryWrite(e.Value);
                }
            }
        }
        finally
        {
            Native.close(fd);
        }
    }

    public void Dispose()
    {
        _cts.Cancel();
        foreach (var t in _threads)
            if (t.IsAlive) t.Join(1000);
        _channel.Writer.TryComplete();
        _cts.Dispose();
    }
}
