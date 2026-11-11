using NeuPod.Platform.Linux;
using NeuPod.Y2Demo;
using NeuPod.Y2Demo.Host;
using NeuPod.Y2Demo.Ui;

namespace NeuPod.Y2.Tests;

public class ArgsTests
{
    [Fact]
    public void Defaults()
    {
        var o = Program.ParseArgs([]);
        Assert.Equal("/etc/neupod-demo.conf", o.Conf);
        Assert.Equal(FbPresentMode.PanPut, o.Mode);
        Assert.Null(o.RenderPngDir);
        Assert.False(o.Help);
    }

    [Fact]
    public void Parses_known_options()
    {
        var o = Program.ParseArgs(["--conf", "/tmp/x.conf", "--fb-mode", "Pan", "--render-png", "/tmp/out"]);
        Assert.Equal("/tmp/x.conf", o.Conf);
        Assert.Equal(FbPresentMode.Pan, o.Mode);
        Assert.Equal("/tmp/out", o.RenderPngDir);
        Assert.True(Program.ParseArgs(["--help"]).Help);
        Assert.True(Program.ParseArgs(["-h"]).Help);
        Assert.True(Program.ParseArgs(["--fb-bench"]).Bench);
    }

    [Theory]
    [InlineData("--bogus")]
    [InlineData("positional")]
    [InlineData("--fb-mode")]
    [InlineData("--conf")]
    public void Rejects_unknown_and_valueless_arguments(string arg) =>
        Assert.Throws<ArgumentException>(() => Program.ParseArgs([arg]));

    [Fact]
    public void Rejects_unknown_fb_mode_value() =>
        Assert.Throws<ArgumentException>(() => Program.ParseArgs(["--fb-mode", "turbo"]));
}

public class ConfFileTests
{
    [Fact]
    public void Save_leaves_no_temp_file_and_skips_unchanged_writes()
    {
        using var fs = new FakeSysfs();
        var path = Path.Combine(fs.Root, "neupod-demo.conf");
        var c = new ConfFile(path);
        c.SaveBacklight(99);
        Assert.Equal(["neupod-demo.conf"], Directory.GetFiles(fs.Root).Select(Path.GetFileName));
        var stamp = File.GetLastWriteTimeUtc(path);
        Thread.Sleep(20);
        c.SaveBacklight(99);
        Assert.Equal(stamp, File.GetLastWriteTimeUtc(path));
    }

    [Fact]
    public void Unwritable_location_does_not_throw()
    {
        new ConfFile("/nonexistent-dir/neupod-demo.conf").SaveBacklight(50);
    }
}

public class ThrowingSysInfoTests
{
    sealed class Boom : ISysInfoSource
    {
        public bool Fail;
        public SysInfoSnapshot Read() => Fail ? throw new InvalidOperationException("boom") : new SysInfoSnapshot { BatteryPercent = 50 };
    }

    [Fact]
    public void Failing_source_keeps_previous_snapshot_and_does_not_crash_tick()
    {
        var clock = new FakeClock(new DateTime(2026, 1, 1, 12, 0, 0));
        var src = new Boom();
        var m = new AppModel(clock, src, new MemoryBacklight(), new RecordingSettings(), "x", "10");
        src.Fail = true;
        clock.Advance(AppModel.BackgroundRefreshMs + 1);
        m.Tick();
        Assert.Equal(50, m.Info.BatteryPercent);
    }

    [Fact]
    public void Failing_source_at_startup_gives_an_empty_snapshot()
    {
        var clock = new FakeClock(new DateTime(2026, 1, 1, 12, 0, 0));
        var m = new AppModel(clock, new Boom { Fail = true }, new MemoryBacklight(), new RecordingSettings(), "x", "10");
        Assert.Null(m.Info.BatteryPercent);
    }

    [Fact]
    public void Reader_survives_unreadable_directories()
    {
        using var fs = new FakeSysfs();
        // A file where a directory is expected, plus an IP lookup that throws something unexpected.
        fs.Write("sys/class/power_supply", "not a directory");
        var s = new SysInfoReader(fs.Root, _ => throw new NotSupportedException("netlink")).Read();
        Assert.Null(s.BatteryPercent);
        Assert.Null(s.Ipv4);
    }
}

public class EvdevLifecycleTests
{
    [Fact]
    public void Constructor_failure_on_second_device_does_not_leak_or_hang()
    {
        Assert.Throws<IOException>(() => new EvdevInput(["/dev/null", "/nonexistent/event9"]));
    }

    [Fact]
    public void Dispose_without_start_is_clean()
    {
        var input = new EvdevInput(["/dev/null"]);
        input.Dispose();
        input.Dispose();
        Assert.True(input.Events.Completion.IsCompleted);
    }

    [Fact]
    public async Task Device_eof_is_logged_and_completes_the_channel_with_an_error()
    {
        var log = new List<string>();
        using var input = new EvdevInput(["/dev/null"], log: log.Add); // read() returns 0 immediately
        input.Start();
        var done = await Task.WhenAny(input.Events.Completion, Task.Delay(5000));
        Assert.Same(input.Events.Completion, done);
        Assert.True(input.Events.Completion.IsFaulted);
        Assert.Contains(log, l => l.Contains("end of file"));
    }
}
