using System.Buffers.Binary;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using NeuPod.Platform.Linux;

namespace NeuPod.Y2.Tests;

public class InputDecodeTests
{
    static byte[] Raw(ushort type, ushort code, int value, int sec = 5, int usec = 7)
    {
        var b = new byte[16];
        BinaryPrimitives.WriteInt32LittleEndian(b, sec);
        BinaryPrimitives.WriteInt32LittleEndian(b.AsSpan(4), usec);
        BinaryPrimitives.WriteUInt16LittleEndian(b.AsSpan(8), type);
        BinaryPrimitives.WriteUInt16LittleEndian(b.AsSpan(10), code);
        BinaryPrimitives.WriteInt32LittleEndian(b.AsSpan(12), value);
        return b;
    }

    [Fact]
    public void Decodes_16_byte_record()
    {
        var e = InputEventDecoder.Decode(Raw(1, 108, 1, sec: 100, usec: 250));
        Assert.Equal(new RawInputEvent(100, 250, 1, 108, 1), e);
    }

    [Fact]
    public void Decodes_negative_value()
    {
        Assert.Equal(-1, InputEventDecoder.Decode(Raw(2, 0, -1)).Value);
    }

    [Fact]
    public void Buffers_partial_reads_across_calls()
    {
        var all = Raw(1, 28, 1).Concat(Raw(1, 28, 0)).ToArray();
        var d = new InputEventDecoder();
        var o = new List<RawInputEvent>();
        d.Feed(all.AsSpan(0, 5), o);
        Assert.Empty(o);
        Assert.Equal(5, d.Pending);
        d.Feed(all.AsSpan(5, 20), o); // completes #1, 9 bytes of #2
        Assert.Single(o);
        Assert.Equal(9, d.Pending);
        d.Feed(all.AsSpan(25), o);
        Assert.Equal(2, o.Count);
        Assert.Equal(0, d.Pending);
        Assert.Equal(0, o[1].Value);
    }

    [Fact]
    public void Handles_many_records_in_one_chunk_with_trailing_partial()
    {
        var chunk = Enumerable.Range(0, 4).SelectMany(i => Raw(1, (ushort)(100 + i), 1)).Concat(new byte[3]).ToArray();
        var d = new InputEventDecoder();
        var o = new List<RawInputEvent>();
        d.Feed(chunk, o);
        Assert.Equal(4, o.Count);
        Assert.Equal(103, o[3].Code);
        Assert.Equal(3, d.Pending);
    }

    [Fact]
    public void Byte_at_a_time_feed_works()
    {
        var d = new InputEventDecoder();
        var o = new List<RawInputEvent>();
        foreach (byte b in Raw(1, 158, 1)) d.Feed([b], o);
        Assert.Single(o);
        Assert.Equal(158, o[0].Code);
    }

    [Fact]
    public void Wheel_press_is_a_tick_and_release_is_dropped()
    {
        Assert.Equal(InputKind.WheelCw, InputMapper.Map(new(0, 0, 1, Keys.WheelDown, 1), 10)?.Kind);
        Assert.Equal(InputKind.WheelCcw, InputMapper.Map(new(0, 0, 1, Keys.WheelUp, 1), 10)?.Kind);
        Assert.Null(InputMapper.Map(new(0, 0, 1, Keys.WheelDown, 0), 10));
        Assert.Null(InputMapper.Map(new(0, 0, 0, 0, 0), 10)); // EV_SYN
    }

    [Theory]
    [InlineData(0, InputKind.KeyRelease)]
    [InlineData(1, InputKind.KeyPress)]
    [InlineData(2, InputKind.KeyRepeat)]
    public void Keys_map_by_value(int value, InputKind kind)
    {
        var e = InputMapper.Map(new(0, 0, 1, Keys.Enter, value), 99);
        Assert.Equal(new InputEvent(kind, Keys.Enter, 99), e);
    }
}

public class DebounceTests
{
    static InputEvent Press(ushort code, long t) => new(InputKind.KeyPress, code, t);
    static InputEvent Tick(long t) => new(InputKind.WheelCw, Keys.WheelDown, t);

    [Fact]
    public void Wheel_tick_right_after_ring_press_is_dropped()
    {
        var d = new RingDebounce(150);
        Assert.True(d.Accept(Press(Keys.Back, 1000)));
        Assert.False(d.Accept(Tick(1100)));
        Assert.True(d.Accept(Tick(1150)));
    }

    [Fact]
    public void Wheel_ticks_pass_without_a_ring_press()
    {
        var d = new RingDebounce();
        Assert.True(d.Accept(Tick(0)));
        Assert.True(d.Accept(Tick(1)));
    }

    [Fact]
    public void Side_keys_do_not_start_the_window()
    {
        var d = new RingDebounce(150);
        Assert.True(d.Accept(Press(Keys.VolumeUp, 1000)));
        Assert.True(d.Accept(Tick(1010)));
    }

    [Fact]
    public void Ring_buttons_are_never_dropped()
    {
        var d = new RingDebounce(150);
        Assert.True(d.Accept(Press(Keys.Enter, 1000)));
        Assert.True(d.Accept(Press(Keys.Left, 1010)));
    }
}

public sealed class FakeSysfs : IDisposable
{
    public string Root { get; } = Directory.CreateTempSubdirectory("y2fake").FullName;
    public string Write(string rel, string text)
    {
        var p = Path.Combine(Root, rel);
        Directory.CreateDirectory(Path.GetDirectoryName(p)!);
        File.WriteAllText(p, text);
        return p;
    }
    public void Dispose() => Directory.Delete(Root, true);
}

public class DiscoveryTests
{
    [Fact]
    public void Finds_devices_by_name_not_by_number()
    {
        using var fs = new FakeSysfs();
        // Deliberately shuffled numbering relative to the real device.
        fs.Write("sys/class/input/event0/device/name", "ACCDET\n");
        fs.Write("sys/class/input/event5/device/name", "mtk-tpd\n");
        fs.Write("sys/class/input/event7/device/name", "mtk-kpd\n");
        fs.Write("sys/class/input/event9/device/name", "mtk-tpd-kpd\n"); // prefix must not match
        fs.Write("sys/class/input/mouse0/device/name", "mtk-tpd\n");     // not an event node
        var found = InputDeviceFinder.Find(["mtk-tpd", "mtk-kpd"], Path.Combine(fs.Root, "sys/class/input"), "/dev/input");
        Assert.Equal(2, found.Count);
        Assert.Equal("/dev/input/event5", found["mtk-tpd"]);
        Assert.Equal("/dev/input/event7", found["mtk-kpd"]);
    }

    [Fact]
    public void Missing_names_and_missing_sysfs_are_skipped()
    {
        using var fs = new FakeSysfs();
        fs.Write("sys/class/input/event1/device/name", "other\n");
        Assert.Empty(InputDeviceFinder.Find(["mtk-tpd"], Path.Combine(fs.Root, "sys/class/input")));
        Assert.Empty(InputDeviceFinder.Find(["mtk-tpd"], Path.Combine(fs.Root, "nope")));
    }
}

public class PixelTests
{
    [Fact]
    public void Xrgb8888_uses_reported_offsets_and_opaque_alpha()
    {
        var p = new PixelPacker(PixelLayout.Xrgb8888);
        Assert.Equal(0xFF123456u, p.Pack(0x123456));
        Assert.Equal(0xFF000000u, p.Pack(0));
    }

    [Fact]
    public void Honours_swapped_channel_offsets()
    {
        // BGRA: red at 0, blue at 16.
        var l = new PixelLayout(32, new(0, 8), new(8, 8), new(16, 8), new(24, 8));
        Assert.Equal(0xFF563412u, new PixelPacker(l).Pack(0x123456));
    }

    [Fact]
    public void Rgb565_truncates_channels()
    {
        var p = new PixelPacker(PixelLayout.Rgb565);
        Assert.Equal(0xFFFFu, p.Pack(0xFFFFFF));
        Assert.Equal(0xF800u, p.Pack(0xFF0000));
        Assert.Equal(0x07E0u, p.Pack(0x00FF00));
        Assert.Equal(0x001Fu, p.Pack(0x0000FF));
    }

    [Fact]
    public void Rejects_unsupported_depth()
    {
        Assert.Throws<NotSupportedException>(() => new PixelPacker(new PixelLayout(8, new(0, 3), new(3, 3), new(6, 2), new(0, 0))));
    }

    [Fact]
    public void Blit_writes_pixels_respecting_stride_and_page()
    {
        var t = new InMemoryFrameTarget(4, 2, 2, PixelLayout.Xrgb8888);
        uint[] src = [1, 2, 3, 4, 5, 6, 7, 8];
        FrameBlit.Blit(src, 4, 2, t, 1, new PixelPacker(PixelLayout.Xrgb8888));
        var px = MemoryMarshal.Cast<byte, uint>(t.Page(1));
        Assert.Equal(0xFF000005u, px[4]);
        Assert.Equal(0xFF000008u, px[7]);
        Assert.All(MemoryMarshal.Cast<byte, uint>(t.Page(0)).ToArray(), v => Assert.Equal(0u, v));
    }

    [Fact]
    public void Fill_covers_the_whole_page()
    {
        var t = new InMemoryFrameTarget(6, 3, 1, PixelLayout.Xrgb8888);
        FrameBlit.Fill(t, 0, 0x202020, new PixelPacker(PixelLayout.Xrgb8888));
        Assert.All(MemoryMarshal.Cast<byte, uint>(t.Page(0)).ToArray(), v => Assert.Equal(0xFF202020u, v));
    }

    [Fact]
    public void Blit_16bpp()
    {
        var t = new InMemoryFrameTarget(2, 1, 1, PixelLayout.Rgb565);
        FrameBlit.Blit([0xFF0000u, 0x0000FFu], 2, 1, t, 0, new PixelPacker(PixelLayout.Rgb565));
        var px = MemoryMarshal.Cast<byte, ushort>(t.Page(0));
        Assert.Equal(0xF800, px[0]);
        Assert.Equal(0x001F, px[1]);
    }
}

public class FbStructTests
{
    [Fact]
    public void Var_screeninfo_is_160_bytes()
    {
        Assert.Equal(160, Unsafe.SizeOf<FbVarScreenInfo>());
    }

    [Fact]
    public void Var_screeninfo_field_offsets_match_the_kernel_header()
    {
        Assert.Equal(32, (int)Marshal.OffsetOf<FbVarScreenInfo>(nameof(FbVarScreenInfo.red)));
        Assert.Equal(68, (int)Marshal.OffsetOf<FbVarScreenInfo>(nameof(FbVarScreenInfo.transp)));
        Assert.Equal(80, (int)Marshal.OffsetOf<FbVarScreenInfo>(nameof(FbVarScreenInfo.nonstd)));
        Assert.Equal(144, (int)Marshal.OffsetOf<FbVarScreenInfo>(nameof(FbVarScreenInfo.reserved0)));
    }

    // fb_fix_screeninfo from <linux/fb.h>: (name, size, align-by-pointer?) in declaration order.
    static readonly (string Name, int Size, bool IsLong)[] FixFields =
    [
        ("id", 16, false), ("smem_start", 0, true), ("smem_len", 4, false), ("type", 4, false), ("type_aux", 4, false),
        ("visual", 4, false), ("xpanstep", 2, false), ("ypanstep", 2, false), ("ywrapstep", 2, false),
        ("line_length", 4, false), ("mmio_start", 0, true), ("mmio_len", 4, false), ("accel", 4, false),
        ("capabilities", 2, false), ("reserved0", 2, false), ("reserved1", 2, false),
    ];

    /// <summary>Natural-alignment layout of the table above for a given sizeof(unsigned long).</summary>
    static (Dictionary<string, int> Offsets, int Size) ComputeFixLayout(int longSize)
    {
        var offsets = new Dictionary<string, int>();
        int off = 0, maxAlign = 1;
        foreach (var (name, size0, isLong) in FixFields)
        {
            int size = isLong ? longSize : size0;
            int align = name == "id" ? 1 : size;
            maxAlign = Math.Max(maxAlign, align);
            off = (off + align - 1) / align * align;
            offsets[name] = off;
            off += size;
        }
        return (offsets, (off + maxAlign - 1) / maxAlign * maxAlign);
    }

    [Fact]
    public void Fix_screeninfo_32bit_layout_is_what_armhf_expects()
    {
        // Hard-coded armhf values (unsigned long = 4): the layout the Y2's kernel uses.
        var (o, size) = ComputeFixLayout(4);
        Assert.Equal(68, size);
        Assert.Equal(16, o["smem_start"]);
        Assert.Equal(20, o["smem_len"]);
        Assert.Equal(36, o["xpanstep"]);
        Assert.Equal(44, o["line_length"]);
        Assert.Equal(48, o["mmio_start"]);
        Assert.Equal(60, o["capabilities"]);
    }

    [Fact]
    public void Fix_screeninfo_struct_matches_the_computed_layout_for_this_pointer_width()
    {
        var (o, size) = ComputeFixLayout(IntPtr.Size);
        Assert.Equal(size, Unsafe.SizeOf<FbFixScreenInfo>());
        foreach (var (name, off) in o)
            Assert.Equal(off, (int)Marshal.OffsetOf<FbFixScreenInfo>(name));
    }

    [Theory]
    [InlineData(2119680u, 1104u, 1920u, 360u, 3)]
    [InlineData(2119680u, 360u, 1920u, 360u, 1)]
    [InlineData(1000u, 360u, 1920u, 360u, 1)]   // mapping smaller than one page: never 0
    [InlineData(2119680u, 1104u, 0u, 360u, 1)]
    public void Max_pages_for_mtkfb_geometry(uint mapLen, uint yv, uint line, uint yres, int pages) =>
        Assert.Equal(pages, Framebuffer.ComputeMaxPages(mapLen, yv, line, yres));
}

public class PresenterTests
{
    sealed class FakeTarget(int pages) : IFrameTarget
    {
        readonly byte[] _mem = new byte[4 * 2 * 4 * 3];
        public int Width => 4;
        public int Height => 2;
        public int StrideBytes => 16;
        public int PageCount { get; set; } = pages;
        public PixelLayout Layout => PixelLayout.Xrgb8888;
        public List<int> Presented { get; } = [];
        public int FailOnCall { get; set; } = -1;
        public List<int> DrawnPages { get; } = [];
        public Span<byte> Page(int index) { DrawnPages.Add(index); return _mem.AsSpan(index * 32, 32); }
        public bool Present(int index)
        {
            Presented.Add(index);
            if (Presented.Count - 1 == FailOnCall) { PageCount = 1; return false; } // driver refused: single page from now on
            return true;
        }
        public void Dispose() { }
    }

    static NeuPod.Y2Demo.Ui.Canvas C() { var c = new NeuPod.Y2Demo.Ui.Canvas(4, 2); c.Clear(0x112233); return c; }

    [Fact]
    public void Cycles_round_robin_over_all_three_pages()
    {
        var t = new FakeTarget(3);
        var p = new NeuPod.Y2Demo.Host.FramePresenter(t);
        for (int i = 0; i < 7; i++) p.Show(C());
        Assert.Equal([0, 1, 2, 0, 1, 2, 0], t.Presented);
    }

    [Fact]
    public void Single_page_target_always_uses_page_zero()
    {
        var t = new FakeTarget(1);
        var p = new NeuPod.Y2Demo.Host.FramePresenter(t);
        for (int i = 0; i < 3; i++) p.Show(C());
        Assert.Equal([0, 0, 0], t.Presented);
    }

    [Fact]
    public void Refused_flip_redraws_onto_page_zero_instead_of_freezing()
    {
        var t = new FakeTarget(3) { FailOnCall = 1 };
        var p = new NeuPod.Y2Demo.Host.FramePresenter(t);
        p.Show(C());  // page 0 ok
        p.Show(C());  // page 1 refused -> redraw + present page 0
        Assert.Equal([0, 1, 0], t.Presented);
        Assert.Equal(1, t.PageCount);
        p.Show(C());
        Assert.Equal([0, 1, 0, 0], t.Presented);
    }

    [Fact]
    public void Fill_also_recovers_from_a_refused_flip()
    {
        var t = new FakeTarget(3) { FailOnCall = 0 };
        new NeuPod.Y2Demo.Host.FramePresenter(t).Fill(0x202020);
        Assert.Equal([0, 0], t.Presented);
    }
}

public class BacklightTests
{
    [Theory]
    [InlineData(-5, 8)]
    [InlineData(0, 8)]
    [InlineData(7, 8)]
    [InlineData(8, 8)]
    [InlineData(100, 100)]
    [InlineData(255, 255)]
    [InlineData(999, 255)]
    public void Clamp_keeps_screen_lit(int input, int expected) => Assert.Equal(expected, Backlight.Clamp(input));

    [Fact]
    public void Set_clamps_and_writes_sysfs()
    {
        using var fs = new FakeSysfs();
        var path = fs.Write("brightness", "128\n");
        var b = new Backlight(path);
        Assert.Equal(128, b.Value);
        b.Set(0);
        Assert.Equal(8, b.Value);
        Assert.Equal("8", File.ReadAllText(path));
    }

    [Fact]
    public void Missing_node_does_not_throw()
    {
        var b = new Backlight("/nonexistent/brightness");
        Assert.False(b.Available);
        b.Set(50);
        Assert.Equal(50, b.Value);
    }
}

public class SysInfoTests
{
    [Fact]
    public void Parses_fixture_tree()
    {
        var s = new SysInfoReader(Path.Combine(AppContext.BaseDirectory, "Fixtures", "y2root"), n => n == "rndis0" ? "192.168.7.2" : null).Read();
        Assert.Equal(632.27, s.Uptime!.Value.TotalSeconds, 2);
        Assert.Equal(8.01, s.Load1);
        Assert.Equal(4.00, s.Load15);
        Assert.Equal(994204, s.MemTotalKb);
        Assert.Equal(970872 + 908 + 4920, s.MemAvailableKb); // 3.4 kernel: no MemAvailable
        Assert.Equal(1, s.OnlineCpus);
        Assert.Equal("3.4.67", s.Kernel);
        Assert.Equal("192.168.7.2", s.Ipv4);
        Assert.Equal(100, s.BatteryPercent);
        Assert.Equal(4117, s.BatteryMillivolts);
        Assert.Equal("Full", s.BatteryStatus);
        Assert.Equal(JackState.Headphones, s.Jack);
    }

    [Fact]
    public void Empty_root_gives_nulls_not_exceptions()
    {
        using var fs = new FakeSysfs();
        var s = new SysInfoReader(fs.Root, _ => null).Read();
        Assert.Null(s.Uptime);
        Assert.Null(s.BatteryPercent);
        Assert.Null(s.Ipv4);
        Assert.Equal(JackState.Unknown, s.Jack);
    }

    [Fact]
    public void Ip_falls_back_to_usb0()
    {
        using var fs = new FakeSysfs();
        var s = new SysInfoReader(fs.Root, n => n == "usb0" ? "10.0.0.2" : null).Read();
        Assert.Equal("10.0.0.2", s.Ipv4);
    }

    [Fact]
    public void Battery_uses_standard_voltage_now_microvolts()
    {
        using var fs = new FakeSysfs();
        fs.Write("sys/class/power_supply/ac/online", "1\n");
        fs.Write("sys/class/power_supply/BAT0/capacity", "55\n");
        fs.Write("sys/class/power_supply/BAT0/voltage_now", "3800000\n");
        var s = new SysInfoReader(fs.Root, _ => null).Read();
        Assert.Equal(55, s.BatteryPercent);
        Assert.Equal(3800, s.BatteryMillivolts);
    }

    [Fact]
    public void Battery_falls_back_to_uevent()
    {
        using var fs = new FakeSysfs();
        fs.Write("sys/class/power_supply/battery/uevent", "POWER_SUPPLY_NAME=battery\nPOWER_SUPPLY_CAPACITY=42\nPOWER_SUPPLY_BATT_VOL=3700\nPOWER_SUPPLY_STATUS=Charging\n");
        var s = new SysInfoReader(fs.Root, _ => null).Read();
        Assert.Equal(42, s.BatteryPercent);
        Assert.Equal(3700, s.BatteryMillivolts);
        Assert.Equal("Charging", s.BatteryStatus);
    }

    [Theory]
    [InlineData("0", 1)]
    [InlineData("0-1", 2)]
    [InlineData("0-1,3\n", 3)]
    [InlineData("0,2-3", 3)]
    public void Cpu_list(string s, int n) => Assert.Equal(n, SysInfoReader.ParseCpuList(s));

    [Theory]
    [InlineData("")]
    [InlineData("x")]
    [InlineData("3-1")]
    public void Cpu_list_bad_input(string s) => Assert.Null(SysInfoReader.ParseCpuList(s));

    [Theory]
    [InlineData("0\n", JackState.None)]
    [InlineData("2\n", JackState.Headphones)]
    [InlineData("1", JackState.Headset)]
    [InlineData("9", JackState.Unknown)]
    [InlineData(null, JackState.Unknown)]
    public void Jack(string? s, JackState j) => Assert.Equal(j, SysInfoReader.ParseJack(s));

    [Fact]
    public void Memavailable_is_preferred_when_present()
    {
        var (t, a) = SysInfoReader.ParseMeminfo("MemTotal: 100 kB\nMemFree: 10 kB\nMemAvailable: 50 kB\n");
        Assert.Equal(100, t);
        Assert.Equal(50, a);
    }
}
