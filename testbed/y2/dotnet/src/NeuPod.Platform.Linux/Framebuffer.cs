using System.ComponentModel;

namespace NeuPod.Platform.Linux;

/// <summary>How a finished page is made visible.</summary>
public enum FbPresentMode
{
    /// <summary>Only write into the mapping; no ioctl. Single page.</summary>
    WriteOnly,
    /// <summary>FBIOPAN_DISPLAY to the page's y offset.</summary>
    Pan,
    /// <summary>FBIOPAN_DISPLAY then FBIOPUT_VSCREENINFO (what fbtool does; mtkfb may only refresh on PUT).</summary>
    PanPut,
}

/// <summary>Linux fbdev via libc open/ioctl/mmap. Layout is taken from the driver, never assumed.</summary>
public sealed unsafe class Framebuffer : IFrameTarget
{
    readonly int _fd;
    readonly nint _map;
    readonly nuint _mapLen;
    FbVarScreenInfo _var;
    bool _disposed;

    Framebuffer(int fd, nint map, nuint mapLen, FbVarScreenInfo v, FbFixScreenInfo f, FbPresentMode mode)
    {
        _fd = fd; _map = map; _mapLen = mapLen; _var = v;
        Width = (int)v.xres;
        Height = (int)v.yres;
        StrideBytes = (int)f.line_length;
        Mode = mode;
        Layout = new PixelLayout((int)v.bits_per_pixel,
            new ColorChannel((int)v.red.offset, (int)v.red.length),
            new ColorChannel((int)v.green.offset, (int)v.green.length),
            new ColorChannel((int)v.blue.offset, (int)v.blue.length),
            new ColorChannel((int)v.transp.offset, (int)v.transp.length));
        long usable = Math.Min((long)mapLen, (long)v.yres_virtual * f.line_length);
        MaxPages = (int)Math.Max(1, usable / ((long)StrideBytes * Height));
        PageCount = mode == FbPresentMode.WriteOnly ? 1 : MaxPages;
    }

    public int Width { get; }
    public int Height { get; }
    public int StrideBytes { get; }
    public PixelLayout Layout { get; }
    public FbPresentMode Mode { get; private set; }
    public int MaxPages { get; }
    public int PageCount { get; private set; }
    public int YOffset => (int)_var.yoffset;

    public static Framebuffer Open(string path = "/dev/fb0", FbPresentMode mode = FbPresentMode.PanPut)
    {
        int fd = Native.open(path, Native.O_RDWR | Native.O_CLOEXEC);
        if (fd < 0) throw new Win32Exception(System.Runtime.InteropServices.Marshal.GetLastWin32Error(), $"open {path}");
        try
        {
            var v = new FbVarScreenInfo();
            var f = new FbFixScreenInfo();
            if (Native.ioctl(fd, Native.FBIOGET_VSCREENINFO, ref v) != 0 || Native.ioctl(fd, Native.FBIOGET_FSCREENINFO, ref f) != 0)
                throw new Win32Exception(System.Runtime.InteropServices.Marshal.GetLastWin32Error(), "FBIOGET_*SCREENINFO");
            if (v.xres == 0 || v.yres == 0 || f.line_length == 0 || v.bits_per_pixel is < 16 or > 32)
                throw new NotSupportedException($"unsupported fb mode {v.xres}x{v.yres} {v.bits_per_pixel}bpp");
            nint map = Native.mmap(0, f.smem_len, Native.PROT_READ | Native.PROT_WRITE, Native.MAP_SHARED, fd, 0);
            if (map == -1) throw new Win32Exception(System.Runtime.InteropServices.Marshal.GetLastWin32Error(), "mmap");
            var fb = new Framebuffer(fd, map, f.smem_len, v, f, mode);
            if (mode == FbPresentMode.WriteOnly) fb.ParkOnPageZero();
            return fb;
        }
        catch
        {
            Native.close(fd);
            throw;
        }
    }

    public Span<byte> Page(int index)
    {
        if ((uint)index >= (uint)MaxPages) throw new ArgumentOutOfRangeException(nameof(index));
        long pageBytes = (long)StrideBytes * Height;
        return new Span<byte>((byte*)_map + index * pageBytes, (int)pageBytes);
    }

    public bool Present(int index)
    {
        if (Mode == FbPresentMode.WriteOnly) return true;
        _var.xoffset = 0;
        _var.yoffset = (uint)(index * Height);
        if (Native.ioctl(_fd, Native.FBIOPAN_DISPLAY, ref _var) != 0) return Degrade();
        if (Mode == FbPresentMode.PanPut)
        {
            _var.activate = 0; // FB_ACTIVATE_NOW
            if (Native.ioctl(_fd, Native.FBIOPUT_VSCREENINFO, ref _var) != 0) return Degrade();
        }
        return true;
    }

    // Write-only draws into page 0, but a previous run may have left the pan offset on another page (we measured
    // yoffset=360 after a page-flipping run). Pan to page 0 once so what we write is what is visible.
    void ParkOnPageZero()
    {
        if (_var.yoffset == 0) return;
        _var.xoffset = 0;
        _var.yoffset = 0;
        Native.ioctl(_fd, Native.FBIOPAN_DISPLAY, ref _var);
        _var.activate = 0;
        Native.ioctl(_fd, Native.FBIOPUT_VSCREENINFO, ref _var);
    }

    // The driver refused paging: carry on write-only so the app still draws instead of crash-looping.
    bool Degrade()
    {
        Mode = FbPresentMode.WriteOnly;
        PageCount = 1;
        return false;
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        Native.munmap(_map, _mapLen);
        Native.close(_fd);
    }
}
