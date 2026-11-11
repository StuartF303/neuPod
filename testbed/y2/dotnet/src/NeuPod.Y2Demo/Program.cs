using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading.Channels;
using NeuPod.Platform.Linux;
using NeuPod.Y2Demo.Host;
using NeuPod.Y2Demo.Ui;

namespace NeuPod.Y2Demo;

public static class Program
{
    const string DefaultConf = "/etc/neupod-demo.conf";

    public static string GitHash =>
        typeof(Program).Assembly.GetCustomAttributes<AssemblyMetadataAttribute>().FirstOrDefault(a => a.Key == "GitHash")?.Value ?? "unknown";

    public static string RuntimeVersion => Environment.Version.ToString();

    public static int Main(string[] args)
    {
        string? Opt(string name) => Array.IndexOf(args, name) is var i and >= 0 && i + 1 < args.Length ? args[i + 1] : null;

        if (Opt("--render-png") is { } dir) return RenderPng(dir);
        string conf = Opt("--conf") ?? DefaultConf;
        var mode = (Opt("--fb-mode") ?? "panput").ToLowerInvariant() switch
        {
            "writeonly" => FbPresentMode.WriteOnly,
            "pan" => FbPresentMode.Pan,
            _ => FbPresentMode.PanPut,
        };
        if (args.Contains("--fb-bench")) return FbBench();
        return Run(conf, mode);
    }

    // ---- device run ----

    static int Run(string confPath, FbPresentMode mode)
    {
        using var cts = new CancellationTokenSource();
        using var s1 = PosixSignalRegistration.Create(PosixSignal.SIGTERM, ctx => { ctx.Cancel = true; cts.Cancel(); });
        using var s2 = PosixSignalRegistration.Create(PosixSignal.SIGINT, ctx => { ctx.Cancel = true; cts.Cancel(); });

        using var fb = Framebuffer.Open(mode: mode);
        Console.WriteLine($"fb {fb.Width}x{fb.Height} stride {fb.StrideBytes} pages {fb.PageCount}/{fb.MaxPages} mode {fb.Mode} " +
                          $"layout {fb.Layout.BitsPerPixel}bpp r{fb.Layout.Red.Offset} g{fb.Layout.Green.Offset} b{fb.Layout.Blue.Offset} a{fb.Layout.Alpha.Offset}");
        var presenter = new FramePresenter(fb);

        var backlight = new Backlight();
        var cf = new ConfFile(confPath);
        if (cf.LoadBacklight() is { } saved) backlight.Set(saved);
        var clock = new SystemClock();
        var model = new AppModel(clock, new SysInfoReader(), backlight, cf, GitHash, RuntimeVersion);
        var renderer = Renderer.CreateDefault();
        var canvas = new Canvas(Renderer.Width, Renderer.Height);

        using var input = EvdevInput.OpenDefault();
        input.Start();
        var reader = input.Events;
        Console.WriteLine($"neuPod Y2 demo {GitHash} running, .NET {RuntimeVersion}");

        try
        {
            while (!cts.IsCancellationRequested)
            {
                if (model.TakeBenchRequest())
                {
                    Patterns.Draw(canvas, model.Pattern);
                    model.SetFlipFps(presenter.MeasureFps(canvas));
                }
                if (model.TakeRedraw())
                {
                    renderer.Render(model, canvas);
                    presenter.Show(canvas);
                }
                WaitForInput(reader, model.NextWakeMs(), cts.Token);
                while (reader.TryRead(out var e)) model.Handle(e);
                model.Tick();
            }
        }
        finally
        {
            model.PersistBacklight();
            presenter.Fill(0x202020);
            Console.WriteLine("shutdown: screen cleared");
        }
        return 0;
    }

    static void WaitForInput(ChannelReader<InputEvent> reader, int timeoutMs, CancellationToken ct)
    {
        if (reader.TryPeek(out _)) return;
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(ct);
        linked.CancelAfter(timeoutMs);
        try { reader.WaitToReadAsync(linked.Token).AsTask().GetAwaiter().GetResult(); }
        catch (OperationCanceledException) { }
    }

    // ---- framebuffer present-mode benchmark ----

    static int FbBench()
    {
        foreach (var mode in new[] { FbPresentMode.WriteOnly, FbPresentMode.Pan, FbPresentMode.PanPut })
        {
            using var fb = Framebuffer.Open(mode: mode);
            var p = new FramePresenter(fb);
            var a = new Canvas(Renderer.Width, Renderer.Height);
            var b = new Canvas(Renderer.Width, Renderer.Height);
            a.Clear(0xFF0000); b.Clear(0x0000FF);
            var sw = System.Diagnostics.Stopwatch.StartNew();
            int n = 0;
            while (n < 60 && (n < 3 || sw.ElapsedMilliseconds < 3000)) { p.Show(n % 2 == 0 ? a : b); n++; }
            double fps = n * 1000.0 / sw.Elapsed.TotalMilliseconds;
            Console.WriteLine($"mode={mode} pages={fb.PageCount}/{fb.MaxPages} frames={n} fps={fps:0.0} yoffset_after={fb.YOffset} mode_after={fb.Mode}");
            Thread.Sleep(1000);
        }
        return 0;
    }

    // ---- host PNG rendering ----

    static int RenderPng(string dir)
    {
        Directory.CreateDirectory(dir);
        var clock = new FakeClock(new DateTime(2026, 11, 11, 14, 32, 0));
        var sys = new FakeSysInfo();
        var model = new AppModel(clock, sys, new MemoryBacklight(160), new RecordingSettings(), GitHash, RuntimeVersion);
        var renderer = Renderer.CreateDefault();
        var canvas = new Canvas(Renderer.Width, Renderer.Height);

        void Snap(string name)
        {
            renderer.Render(model, canvas);
            File.WriteAllBytes(Path.Combine(dir, name + ".png"), Png.Encode(canvas));
            Console.WriteLine("wrote " + name + ".png");
        }
        void Press(ushort code, long dt = 120)
        {
            clock.Advance(dt);
            model.Handle(new InputEvent(InputKind.KeyPress, code, clock.NowMs));
            clock.Advance(40);
            model.Handle(new InputEvent(InputKind.KeyRelease, code, clock.NowMs));
        }
        void Wheel(bool cw, long dt = 60)
        {
            clock.Advance(dt);
            model.Handle(new InputEvent(cw ? InputKind.WheelCw : InputKind.WheelCcw, cw ? Keys.WheelDown : Keys.WheelUp, clock.NowMs));
        }

        Snap("01-main");
        Wheel(true); Snap("01b-main-selected");
        Wheel(false);

        Press(Keys.Enter);
        // Input test: a short Menu press is only logged; the log shows a believable mix.
        Wheel(true); Wheel(true); Wheel(false); Press(Keys.Left); Press(Keys.Back); Press(Keys.PlayPause); Press(Keys.Enter);
        Snap("02-input-test");
        model.Handle(new InputEvent(InputKind.KeyPress, Keys.Back, clock.Advance2(100)));
        clock.Advance(500); model.Tick();
        Snap("02b-input-test-menu-held");
        clock.Advance(600); model.Tick(); // hold passed 1 s: back to main

        Wheel(true); Press(Keys.Enter);
        model.SetFlipFps(27.4);
        foreach (var p in Enum.GetValues<Pattern>())
        {
            Snap($"03-display-{(int)p + 1:00}-{p.ToString().ToLowerInvariant()}");
            Wheel(true);
        }
        Press(Keys.Back);

        Wheel(true); Press(Keys.Enter);
        Snap("04-backlight");
        Press(Keys.Back);

        Wheel(true); Press(Keys.Enter);
        Snap("05-system");
        sys.Snapshot = sys.Snapshot with { BatteryPercent = null, Jack = JackState.None, Ipv4 = null };
        clock.Advance(1500); model.Tick();
        Snap("05b-system-missing-data");
        Press(Keys.Back);

        Wheel(true); Press(Keys.Enter);
        Snap("06-about");
        return 0;
    }

    static long Advance2(this FakeClock c, long ms) { c.Advance(ms); return c.NowMs; }
}
