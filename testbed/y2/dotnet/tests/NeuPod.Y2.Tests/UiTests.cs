using NeuPod.Platform.Linux;
using NeuPod.Y2Demo.Host;
using NeuPod.Y2Demo.Ui;

namespace NeuPod.Y2.Tests;

public class ModelHarness
{
    public FakeClock Clock { get; } = new(new DateTime(2026, 11, 11, 12, 0, 0));
    public FakeSysInfo Sys { get; } = new();
    public MemoryBacklight Light { get; } = new(128);
    public RecordingSettings Store { get; } = new();
    public AppModel Model { get; }

    public ModelHarness() => Model = new AppModel(Clock, Sys, Light, Store, "abc1234", "10.0.0");

    public void Down(ushort code, long dt = 100) { Clock.Advance(dt); Model.Handle(new(InputKind.KeyPress, code, Clock.NowMs)); }
    public void Up(ushort code, long dt = 30) { Clock.Advance(dt); Model.Handle(new(InputKind.KeyRelease, code, Clock.NowMs)); }
    public void Press(ushort code) { Down(code); Up(code); }
    public void Cw(long dt = 50) { Clock.Advance(dt); Model.Handle(new(InputKind.WheelCw, Keys.WheelDown, Clock.NowMs)); }
    public void Ccw(long dt = 50) { Clock.Advance(dt); Model.Handle(new(InputKind.WheelCcw, Keys.WheelUp, Clock.NowMs)); }
    public void Go(ScreenId id)
    {
        while (Model.Menu.Selected < Array.FindIndex(AppModel.MenuItems, m => m.Id == id)) Cw();
        Press(Keys.Enter);
        Assert.Equal(id, Model.Screen);
    }
}

public class NavigationTests
{
    [Fact]
    public void Starts_on_main_menu()
    {
        var h = new ModelHarness();
        Assert.Equal(ScreenId.Main, h.Model.Screen);
        Assert.Equal("neuPod Y2", h.Model.Title);
        Assert.Equal(5, AppModel.MenuItems.Length);
    }

    [Fact]
    public void Wheel_moves_selection_and_clamps_at_ends()
    {
        var h = new ModelHarness();
        h.Ccw();
        Assert.Equal(0, h.Model.Menu.Selected);
        for (int i = 0; i < 10; i++) h.Cw();
        Assert.Equal(4, h.Model.Menu.Selected);
    }

    [Fact]
    public void Centre_enters_and_menu_goes_back()
    {
        var h = new ModelHarness();
        h.Cw(); h.Cw();
        h.Press(Keys.Enter);
        Assert.Equal(ScreenId.Backlight, h.Model.Screen);
        h.Press(Keys.Back);
        Assert.Equal(ScreenId.Main, h.Model.Screen);
        Assert.Equal(2, h.Model.Menu.Selected); // selection survives
    }

    [Theory]
    [InlineData(ScreenId.DisplayTest)]
    [InlineData(ScreenId.Backlight)]
    [InlineData(ScreenId.System)]
    [InlineData(ScreenId.About)]
    public void Menu_press_leaves_every_ordinary_screen(ScreenId id)
    {
        var h = new ModelHarness();
        h.Go(id);
        h.Press(Keys.Back);
        Assert.Equal(ScreenId.Main, h.Model.Screen);
    }

    [Fact]
    public void Menu_on_main_does_nothing()
    {
        var h = new ModelHarness();
        h.Press(Keys.Back);
        Assert.Equal(ScreenId.Main, h.Model.Screen);
    }

    [Fact]
    public void List_scrolls_to_keep_selection_visible()
    {
        var l = new ListState(20, 5);
        for (int i = 0; i < 7; i++) l.Move(1);
        Assert.Equal(7, l.Selected);
        Assert.Equal(3, l.Top);
        for (int i = 0; i < 7; i++) l.Move(-1);
        Assert.Equal(0, l.Selected);
        Assert.Equal(0, l.Top);
        Assert.False(l.Move(-1));
    }
}

public class InputTestScreenTests
{
    [Fact]
    public void Short_menu_press_is_only_logged()
    {
        var h = new ModelHarness();
        h.Go(ScreenId.InputTest);
        h.Down(Keys.Back);
        h.Clock.Advance(300); h.Model.Tick();
        h.Up(Keys.Back, 100);
        Assert.Equal(ScreenId.InputTest, h.Model.Screen);
        // (the entry before these is the release of the Centre press that opened the screen)
        var tail = h.Model.InputLog.TakeLast(2).ToList();
        Assert.Equal(["press", "release"], tail.Select(e => e.Action));
        Assert.All(tail, e => Assert.Equal("KEY_BACK", e.Name));
        Assert.False(h.Model.BackHeld);
    }

    [Fact]
    public void Holding_menu_for_a_second_leaves_via_timer()
    {
        var h = new ModelHarness();
        h.Go(ScreenId.InputTest);
        h.Down(Keys.Back);
        h.Clock.Advance(999); h.Model.Tick();
        Assert.Equal(ScreenId.InputTest, h.Model.Screen);
        h.Clock.Advance(1); h.Model.Tick();
        Assert.Equal(ScreenId.Main, h.Model.Screen);
    }

    [Fact]
    public void Holding_then_releasing_late_also_leaves()
    {
        var h = new ModelHarness();
        h.Go(ScreenId.InputTest);
        h.Down(Keys.Back);
        h.Up(Keys.Back, 1200); // no tick in between
        Assert.Equal(ScreenId.Main, h.Model.Screen);
    }

    [Fact]
    public void Release_after_a_timer_exit_is_ignored_on_the_main_screen()
    {
        var h = new ModelHarness();
        h.Go(ScreenId.InputTest);
        h.Down(Keys.Back);
        h.Clock.Advance(1000); h.Model.Tick();
        Assert.Equal(ScreenId.Main, h.Model.Screen);
        h.Up(Keys.Back);
        Assert.Equal(ScreenId.Main, h.Model.Screen);
    }

    [Fact]
    public void Log_keeps_last_eight_with_deltas()
    {
        var h = new ModelHarness();
        h.Go(ScreenId.InputTest);
        for (int i = 0; i < 12; i++) h.Cw(dt: 10 + i);
        Assert.Equal(AppModel.InputLogSize, h.Model.InputLog.Count);
        Assert.Equal(21, h.Model.InputLog[^1].DeltaMs);
        Assert.Equal("WHEEL", h.Model.InputLog[^1].Name);
        Assert.Equal("cw", h.Model.InputLog[^1].Action);
    }

    [Fact]
    public void Counters_count_presses_and_wheel_directions()
    {
        var h = new ModelHarness();
        h.Go(ScreenId.InputTest);
        h.Press(Keys.PlayPause); h.Press(Keys.PlayPause); h.Cw(); h.Ccw(); h.Ccw();
        Assert.Equal(2, h.Model.Counters["KEY_PLAYPAUSE"]);
        Assert.Equal(1, h.Model.Counters["WHEEL CW"]);
        Assert.Equal(2, h.Model.Counters["WHEEL CCW"]);
    }

    [Fact]
    public void Wheel_rate_counts_ticks_in_the_last_second()
    {
        var h = new ModelHarness();
        h.Go(ScreenId.InputTest);
        for (int i = 0; i < 5; i++) h.Cw(dt: 100);
        Assert.Equal(5, h.Model.WheelTicksPerSecond(h.Clock.NowMs));
        Assert.Equal(4, h.Model.WheelTicksPerSecond(h.Clock.NowMs + 600));
        Assert.Equal(0, h.Model.WheelTicksPerSecond(h.Clock.NowMs + 2000));
    }

    [Fact]
    public void Volume_keys_still_adjust_backlight_while_logged()
    {
        var h = new ModelHarness();
        h.Go(ScreenId.InputTest);
        h.Press(Keys.VolumeUp);
        Assert.Equal(144, h.Light.Value);
        Assert.Equal("KEY_VOLUMEUP", h.Model.InputLog[^2].Name); // press, then release
    }
}

public class DisplayTestTests
{
    [Fact]
    public void Wheel_cycles_patterns_both_ways_with_wrap_without_remeasuring()
    {
        var h = new ModelHarness();
        h.Go(ScreenId.DisplayTest);
        Assert.True(h.Model.TakeBenchRequest());
        Assert.Equal(Pattern.ColorBars, h.Model.Pattern);
        h.Model.SetFlipFps(40);
        h.Cw();
        Assert.Equal(Pattern.Red, h.Model.Pattern);
        Assert.False(h.Model.TakeBenchRequest()); // cycling must not re-run the blocking measurement
        Assert.Equal(40, h.Model.FlipFps);
        h.Ccw(); h.Ccw();
        Assert.Equal(Pattern.Checker, h.Model.Pattern);
    }

    [Fact]
    public void Play_pause_toggles_overlay_and_centre_remeasures()
    {
        var h = new ModelHarness();
        h.Go(ScreenId.DisplayTest);
        h.Model.TakeBenchRequest();
        Assert.True(h.Model.ShowOverlay);
        h.Press(Keys.PlayPause);
        Assert.False(h.Model.ShowOverlay);
        h.Press(Keys.PlayPause);
        Assert.True(h.Model.ShowOverlay);
        h.Press(Keys.Enter);
        Assert.True(h.Model.TakeBenchRequest());
        h.Model.SetFlipFps(30.5);
        Assert.Equal(30.5, h.Model.FlipFps);
    }
}

public class BacklightScreenTests
{
    [Fact]
    public void Wheel_adjusts_in_steps_and_floors_at_minimum()
    {
        var h = new ModelHarness();
        h.Go(ScreenId.Backlight);
        h.Cw();
        Assert.Equal(136, h.Light.Value);
        for (int i = 0; i < 100; i++) h.Ccw();
        Assert.Equal(Backlight.Min, h.Light.Value);
        for (int i = 0; i < 100; i++) h.Cw();
        Assert.Equal(255, h.Light.Value);
    }

    [Fact]
    public void Volume_keys_work_from_any_screen()
    {
        var h = new ModelHarness();
        h.Press(Keys.VolumeDown);
        Assert.Equal(112, h.Light.Value);
        h.Go(ScreenId.About);
        h.Press(Keys.VolumeUp);
        Assert.Equal(128, h.Light.Value);
    }

    [Fact]
    public void Persists_only_on_leaving_the_screen()
    {
        var h = new ModelHarness();
        h.Go(ScreenId.Backlight);
        h.Cw(); h.Cw();
        Assert.Empty(h.Store.Saved);
        h.Press(Keys.Back);
        Assert.Equal([144], h.Store.Saved);
    }

    [Fact]
    public void Leaving_unchanged_writes_nothing()
    {
        var h = new ModelHarness();
        h.Go(ScreenId.Backlight);
        h.Cw(); h.Ccw();
        h.Press(Keys.Back);
        Assert.Empty(h.Store.Saved);
    }

    [Fact]
    public void Shutdown_flush_saves_volume_changes_once()
    {
        var h = new ModelHarness();
        h.Press(Keys.VolumeUp);
        h.Model.PersistBacklight();
        h.Model.PersistBacklight();
        Assert.Equal([144], h.Store.Saved);
    }

    [Fact]
    public void Conf_file_round_trip_and_clamp()
    {
        using var fs = new FakeSysfs();
        var path = Path.Combine(fs.Root, "neupod-demo.conf");
        var c = new ConfFile(path);
        Assert.Null(c.LoadBacklight());
        c.SaveBacklight(200);
        Assert.Equal(200, c.LoadBacklight());
        File.WriteAllText(path, "backlight=1\n");
        Assert.Equal(8, c.LoadBacklight());
        File.WriteAllText(path, "garbage\n");
        Assert.Null(c.LoadBacklight());
    }
}

public class TimerTests
{
    [Fact]
    public void System_screen_refreshes_at_1hz_and_other_screens_rarely()
    {
        var h = new ModelHarness();
        h.Go(ScreenId.System);
        int baseline = h.Sys.Reads;
        h.Clock.Advance(1000); h.Model.Tick();
        Assert.Equal(baseline + 1, h.Sys.Reads);
        h.Clock.Advance(500); h.Model.Tick();
        Assert.Equal(baseline + 1, h.Sys.Reads);

        h.Press(Keys.Back);
        int onMain = h.Sys.Reads;
        h.Clock.Advance(5000); h.Model.Tick();
        Assert.Equal(onMain, h.Sys.Reads);
    }

    [Fact]
    public void Idle_main_screen_sleeps_until_the_next_minute_at_most()
    {
        var h = new ModelHarness();
        h.Model.Tick();
        h.Model.TakeRedraw();
        int wake = h.Model.NextWakeMs();
        Assert.InRange(wake, 1000, 60_020);
        Assert.False(h.Model.TakeRedraw()); // nothing pending, so no busy loop
    }

    [Fact]
    public void Clock_minute_change_triggers_redraw()
    {
        var h = new ModelHarness();
        h.Model.Tick(); h.Model.TakeRedraw();
        h.Clock.Advance(61_000); h.Model.Tick();
        Assert.True(h.Model.TakeRedraw());
    }

    [Fact]
    public void Input_test_wakes_quickly_only_while_holding_menu()
    {
        var h = new ModelHarness();
        h.Go(ScreenId.InputTest);
        h.Model.Tick(); h.Model.TakeRedraw();
        Assert.True(h.Model.NextWakeMs() > 1000);
        h.Down(Keys.Back);
        Assert.True(h.Model.NextWakeMs() <= 100);
    }
}

public class RenderTests
{
    static readonly Renderer R = Renderer.CreateDefault();

    [Fact]
    public void Font_parses_with_expected_cell_sizes()
    {
        Assert.Equal((8, 16), (R.Small.CellWidth, R.Small.CellHeight));
        Assert.Equal((12, 24), (R.Large.CellWidth, R.Large.CellHeight));
        Assert.True(R.Small.GlyphCount > 200);
        Assert.True(R.Small.Has('A'));
    }

    [Fact]
    public void Glyph_rendering_draws_ink_for_letters_and_nothing_for_space()
    {
        var c = new Canvas(40, 24);
        c.Clear(0);
        c.Text(R.Small, 0, 0, " ", 0xFFFFFF);
        Assert.All(c.Pixels, p => Assert.Equal(0u, p));
        c.Text(R.Small, 8, 4, "I", 0xFFFFFF);
        int ink = c.Pixels.Count(p => p != 0);
        Assert.InRange(ink, 8, 60);
        // Nothing outside the glyph's own cell.
        for (int y = 0; y < 24; y++)
            for (int x = 0; x < 40; x++)
                if (c.Get(x, y) != 0) Assert.True(x is >= 8 and < 16 && y is >= 4 and < 20, $"stray pixel {x},{y}");
    }

    [Fact]
    public void Bitmap_rows_map_msb_to_leftmost_pixel()
    {
        // Hand-made 8x2 font: glyph 'A' = 10000001 / 01000010.
        string bdf = "STARTFONT 2.1\nFONTBOUNDINGBOX 8 2 0 0\nFONT_ASCENT 2\nDEFAULT_CHAR 65\nCHARS 1\n" +
                     "STARTCHAR A\nENCODING 65\nDWIDTH 8 0\nBBX 8 2 0 0\nBITMAP\n81\n42\nENDCHAR\nENDFONT\n";
        var f = BdfFont.Parse(bdf);
        var c = new Canvas(8, 2);
        c.Clear(0);
        c.Text(f, 0, 0, "A", 0xFFFFFF);
        Assert.Equal(0xFFFFFFu, c.Get(0, 0));
        Assert.Equal(0xFFFFFFu, c.Get(7, 0));
        Assert.Equal(0u, c.Get(1, 0));
        Assert.Equal(0xFFFFFFu, c.Get(1, 1));
        Assert.Equal(0xFFFFFFu, c.Get(6, 1));
        Assert.Equal(0u, c.Get(0, 1));
    }

    [Fact]
    public void Unknown_characters_fall_back_to_the_default_glyph()
    {
        Assert.False(R.Small.Has('\u2603'));
        Assert.Same(R.Small.Glyph((char)R.Small.DefaultChar), R.Small.Glyph('\u2603'));
    }

    [Fact]
    public void Colour_bars_pattern_has_eight_columns()
    {
        var c = new Canvas(480, 360);
        Patterns.Draw(c, Pattern.ColorBars);
        for (int i = 0; i < 8; i++) Assert.Equal(Patterns.BarColors[i], c.Get(i * 60 + 30, 100));
    }

    [Fact]
    public void Checker_alternates_every_pixel_and_ramp_spans_black_to_white()
    {
        var c = new Canvas(480, 360);
        Patterns.Draw(c, Pattern.Checker);
        Assert.Equal(0xFFFFFFu, c.Get(0, 0));
        Assert.Equal(0u, c.Get(1, 0));
        Assert.Equal(0u, c.Get(0, 1));
        Patterns.Draw(c, Pattern.GreyRamp);
        Assert.Equal(0u, c.Get(0, 5));
        Assert.Equal(0xFFFFFFu, c.Get(479, 5));
    }

    [Fact]
    public void Every_screen_renders_and_encodes_to_a_valid_png()
    {
        var h = new ModelHarness();
        var c = new Canvas(Renderer.Width, Renderer.Height);
        foreach (var id in Enum.GetValues<ScreenId>().Where(s => s != ScreenId.Main))
        {
            h.Go(id);
            R.Render(h.Model, c);
            var png = Png.Encode(c);
            Assert.Equal([0x89, 0x50, 0x4E, 0x47], png[..4]);
            Assert.Contains(c.Pixels, p => p != c.Pixels[0]);
            h.Press(Keys.Back);
            if (id == ScreenId.InputTest) { h.Down(Keys.Back); h.Clock.Advance(1000); h.Model.Tick(); }
        }
    }

    [Fact]
    public void Png_crc_matches_known_vector()
    {
        Assert.Equal(0xCBF43926u, Png.Crc32("123456789"u8));
    }
}
