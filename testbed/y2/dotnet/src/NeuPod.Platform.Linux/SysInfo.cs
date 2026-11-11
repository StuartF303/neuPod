using System.Globalization;
using System.Net.NetworkInformation;
using System.Net.Sockets;

namespace NeuPod.Platform.Linux;

public enum JackState { Unknown, None, Headphones, Headset }

public sealed record SysInfoSnapshot
{
    public TimeSpan? Uptime { get; init; }
    public double? Load1 { get; init; }
    public double? Load5 { get; init; }
    public double? Load15 { get; init; }
    public long? MemTotalKb { get; init; }
    public long? MemAvailableKb { get; init; }
    public int? OnlineCpus { get; init; }
    public string? Kernel { get; init; }
    public string? Ipv4 { get; init; }
    public int? BatteryPercent { get; init; }
    public int? BatteryMillivolts { get; init; }
    public string? BatteryStatus { get; init; }
    public JackState Jack { get; init; }
}

public interface ISysInfoSource
{
    SysInfoSnapshot Read();
}

/// <summary>Reads /proc and /sys under <c>root</c> (so tests can point it at fixture files). Every field is best effort.</summary>
public sealed class SysInfoReader(string root = "/", Func<string, string?>? ipLookup = null) : ISysInfoSource
{
    public static readonly string[] NetInterfaces = ["rndis0", "usb0"];

    readonly Func<string, string?> _ip = ipLookup ?? DefaultIpLookup;

    public SysInfoSnapshot Read()
    {
        var (l1, l5, l15) = ParseLoadavg(Text("proc/loadavg"));
        var (total, avail) = ParseMeminfo(Text("proc/meminfo"));
        var (pct, mv, status) = ReadBattery();
        return new SysInfoSnapshot
        {
            Uptime = ParseUptime(Text("proc/uptime")),
            Load1 = l1, Load5 = l5, Load15 = l15,
            MemTotalKb = total, MemAvailableKb = avail,
            OnlineCpus = ParseCpuList(Text("sys/devices/system/cpu/online")),
            Kernel = Text("proc/sys/kernel/osrelease")?.Trim(),
            Ipv4 = FirstIp(),
            BatteryPercent = pct, BatteryMillivolts = mv, BatteryStatus = status,
            Jack = ParseJack(Text("sys/class/switch/h2w/state")),
        };
    }

    string? Text(string rel)
    {
        try { return File.ReadAllText(Path.Combine(root, rel)); }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { return null; }
    }

    string? FirstIp()
    {
        foreach (var n in NetInterfaces)
        {
            string? ip;
            try { ip = _ip(n); } catch (Exception e) when (e is NetworkInformationException or InvalidOperationException) { ip = null; }
            if (!string.IsNullOrEmpty(ip)) return ip;
        }
        return null;
    }

    (int? pct, int? mv, string? status) ReadBattery()
    {
        string dir = Path.Combine(root, "sys/class/power_supply");
        if (!Directory.Exists(dir)) return (null, null, null);
        // Prefer "battery"; otherwise the first supply that has a capacity file (AC/USB supplies do not).
        var candidates = Directory.GetDirectories(dir).OrderBy(d => Path.GetFileName(d) == "battery" ? 0 : 1).ThenBy(d => d, StringComparer.Ordinal);
        foreach (var d in candidates)
        {
            string? cap = Read(Path.Combine(d, "capacity"));
            var uev = ParseUevent(Read(Path.Combine(d, "uevent")));
            if (cap is null && !uev.ContainsKey("POWER_SUPPLY_CAPACITY")) continue;
            int? pct = ParseInt(cap) ?? ParseInt(uev.GetValueOrDefault("POWER_SUPPLY_CAPACITY"));
            int? mv = null;
            if (ParseInt(Read(Path.Combine(d, "voltage_now"))) is { } uv) mv = uv / 1000; // standard unit: microvolts
            else if (ParseInt(Read(Path.Combine(d, "batt_vol"))) is { } bv) mv = bv;        // MediaTek: millivolts
            else if (ParseInt(uev.GetValueOrDefault("POWER_SUPPLY_BATT_VOL")) is { } uvv) mv = uvv;
            string? status = Read(Path.Combine(d, "status"))?.Trim() ?? uev.GetValueOrDefault("POWER_SUPPLY_STATUS");
            return (pct, mv, status);
        }
        return (null, null, null);

        static string? Read(string p)
        {
            try { return File.ReadAllText(p); }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { return null; }
        }
    }

    static int? ParseInt(string? s) =>
        int.TryParse(s?.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out int v) ? v : null;

    public static string? DefaultIpLookup(string ifaceName)
    {
        foreach (var ni in NetworkInterface.GetAllNetworkInterfaces())
        {
            if (ni.Name != ifaceName) continue;
            foreach (var ua in ni.GetIPProperties().UnicastAddresses)
                if (ua.Address.AddressFamily == AddressFamily.InterNetwork) return ua.Address.ToString();
        }
        return null;
    }

    /// <summary>"632.27 993.94" -> 632.27 s.</summary>
    public static TimeSpan? ParseUptime(string? s)
    {
        var first = s?.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries).FirstOrDefault();
        return double.TryParse(first, NumberStyles.Float, CultureInfo.InvariantCulture, out double sec) && sec >= 0
            ? TimeSpan.FromSeconds(sec) : null;
    }

    public static (double? l1, double? l5, double? l15) ParseLoadavg(string? s)
    {
        var p = s?.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        if (p is null || p.Length < 3) return (null, null, null);
        double? D(string x) => double.TryParse(x, NumberStyles.Float, CultureInfo.InvariantCulture, out double v) ? v : null;
        return (D(p[0]), D(p[1]), D(p[2]));
    }

    /// <summary>MemTotal and "available" in kB. Kernels before 3.14 (the Y2's 3.4) lack MemAvailable, so approximate with free+buffers+cached.</summary>
    public static (long? total, long? available) ParseMeminfo(string? s)
    {
        if (s is null) return (null, null);
        var map = new Dictionary<string, long>(StringComparer.Ordinal);
        foreach (var line in s.Split('\n'))
        {
            int c = line.IndexOf(':');
            if (c < 0) continue;
            var parts = line[(c + 1)..].Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length > 0 && long.TryParse(parts[0], NumberStyles.Integer, CultureInfo.InvariantCulture, out long kb))
                map[line[..c]] = kb;
        }
        long? total = map.TryGetValue("MemTotal", out long t) ? t : null;
        long? avail = map.TryGetValue("MemAvailable", out long a) ? a
            : map.TryGetValue("MemFree", out long f) ? f + map.GetValueOrDefault("Buffers") + map.GetValueOrDefault("Cached") : null;
        return (total, avail);
    }

    /// <summary>"0-1,3" -> 3 CPUs. "0" -> 1.</summary>
    public static int? ParseCpuList(string? s)
    {
        if (string.IsNullOrWhiteSpace(s)) return null;
        int count = 0;
        foreach (var part in s.Trim().Split(',', StringSplitOptions.RemoveEmptyEntries))
        {
            var ends = part.Split('-');
            if (!int.TryParse(ends[0], NumberStyles.Integer, CultureInfo.InvariantCulture, out int lo)) return null;
            int hi = lo;
            if (ends.Length == 2 && !int.TryParse(ends[1], NumberStyles.Integer, CultureInfo.InvariantCulture, out hi)) return null;
            if (hi < lo) return null;
            count += hi - lo + 1;
        }
        return count;
    }

    public static JackState ParseJack(string? s) => int.TryParse(s?.Trim(), out int v) ? v switch
    {
        0 => JackState.None,
        1 => JackState.Headset,
        2 => JackState.Headphones,
        _ => JackState.Unknown,
    } : JackState.Unknown;

    public static Dictionary<string, string> ParseUevent(string? s)
    {
        var d = new Dictionary<string, string>(StringComparer.Ordinal);
        if (s is null) return d;
        foreach (var line in s.Split('\n'))
        {
            int eq = line.IndexOf('=');
            if (eq > 0) d[line[..eq]] = line[(eq + 1)..].Trim();
        }
        return d;
    }
}
