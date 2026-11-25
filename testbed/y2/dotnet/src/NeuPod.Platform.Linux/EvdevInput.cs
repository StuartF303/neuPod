using System.Runtime.InteropServices;
using System.Threading.Channels;
using Microsoft.Win32.SafeHandles;

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
/// Reads evdev devices on background threads (blocking read behind a poll timeout so Dispose is prompt) and
/// publishes typed, debounced events on <see cref="Events"/>. The channel completes (with an error) only when
/// every device has failed, so a consumer must treat completion as fatal rather than wait on it again.
/// </summary>
public sealed unsafe class EvdevInput : IDisposable
{
    public static readonly string[] DefaultDeviceNames = ["mtk-tpd", "mtk-kpd"];

    const int EINTR = 4, EAGAIN = 11;

    readonly Channel<InputEvent> _channel = Channel.CreateUnbounded<InputEvent>(new UnboundedChannelOptions { SingleReader = true });
    readonly List<(Thread Thread, SafeFileHandle Handle, string Path)> _devices = [];
    readonly CancellationTokenSource _cts = new();
    readonly RingDebounce _debounce;
    readonly Func<long> _clock;
    readonly Action<string> _log;
    readonly object _gate = new();
    int _live;
    bool _started, _disposed;

    public EvdevInput(IEnumerable<string> devicePaths, RingDebounce? debounce = null, Func<long>? clock = null, Action<string>? log = null)
    {
        _debounce = debounce ?? new RingDebounce();
        _clock = clock ?? (() => Environment.TickCount64); // monotonic ms, no multiplication to overflow
        _log = log ?? (_ => { });
        try
        {
            foreach (var path in devicePaths)
            {
                int fd = Native.open(path, Native.O_RDONLY | Native.O_CLOEXEC);
                if (fd < 0)
                    throw new IOException($"cannot open {path} (errno {Marshal.GetLastPInvokeError()})");
                var handle = new SafeFileHandle(fd, ownsHandle: true);
                var t = new Thread(() => ReadLoop(handle, path)) { IsBackground = true, Name = "evdev " + path };
                _devices.Add((t, handle, path));
            }
        }
        catch
        {
            foreach (var d in _devices) d.Handle.Dispose();
            throw;
        }
        _live = _devices.Count;
    }

    /// <summary>Discover the default Y2 devices by name and open them. Throws if none are found.</summary>
    public static EvdevInput OpenDefault(string sysClassInput = InputDeviceFinder.DefaultSysClassInput, string devInput = InputDeviceFinder.DefaultDevInput, Action<string>? log = null)
    {
        var found = InputDeviceFinder.Find(DefaultDeviceNames, sysClassInput, devInput);
        if (found.Count == 0) throw new IOException("no input devices found (" + string.Join(", ", DefaultDeviceNames) + ")");
        return new EvdevInput(found.Values, log: log);
    }

    public ChannelReader<InputEvent> Events => _channel.Reader;

    public void Start()
    {
        if (_started) return;
        _started = true;
        foreach (var d in _devices) d.Thread.Start();
    }

    void ReadLoop(SafeFileHandle handle, string path)
    {
        var decoder = new InputEventDecoder();
        var raw = new List<RawInputEvent>();
        var buf = new byte[RawInputEvent.Size * 32];
        string? fatal = null;
        try
        {
            int fd = (int)handle.DangerousGetHandle();
            while (!_cts.IsCancellationRequested)
            {
                var pfd = new Native.PollFd { fd = fd, events = Native.POLLIN };
                int pr = Native.poll(&pfd, 1, 250);
                if (pr < 0)
                {
                    int err = Marshal.GetLastPInvokeError();
                    if (err == EINTR) continue;
                    fatal = $"poll failed (errno {err})";
                    break;
                }
                if (pr == 0) continue; // timeout; re-check cancellation
                if ((pfd.revents & ~Native.POLLIN) != 0 && (pfd.revents & Native.POLLIN) == 0)
                {
                    fatal = $"device error/hangup (revents 0x{pfd.revents:x})"; // POLLERR/POLLHUP/POLLNVAL, e.g. unplugged
                    break;
                }
                nint n;
                fixed (byte* p = buf) n = Native.read(fd, p, (nuint)buf.Length);
                if (n < 0)
                {
                    int err = Marshal.GetLastPInvokeError();
                    if (err is EINTR or EAGAIN) continue;
                    fatal = $"read failed (errno {err}, ENODEV=19 means the device went away)";
                    break;
                }
                if (n == 0) { fatal = "end of file"; break; }
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
        catch (Exception ex)
        {
            fatal = ex.Message;
        }
        finally
        {
            if (fatal is not null) _log($"input {path}: {fatal}; reader stopped");
            if (Interlocked.Decrement(ref _live) == 0 && !_cts.IsCancellationRequested)
                _channel.Writer.TryComplete(new IOException("all input devices failed"));
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _cts.Cancel();
        // Handles are closed only after the readers have stopped (or never started), so no thread polls a closed fd.
        foreach (var d in _devices)
            if (_started && d.Thread.IsAlive) d.Thread.Join(2000);
        foreach (var d in _devices) d.Handle.Dispose();
        _channel.Writer.TryComplete();
        _cts.Dispose();
    }
}
