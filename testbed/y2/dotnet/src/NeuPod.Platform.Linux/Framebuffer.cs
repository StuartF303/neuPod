using System.ComponentModel;
using System.Runtime.InteropServices;

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
    // Page that single-page (write-only) drawing goes to: the page we believe is actually visible.
    int _drawPage;
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
        MaxPages = ComputeMaxPages(mapLen, v.yres_virtual, f.line_length, v.yres);
        PageCount = mode == FbPresentMode.WriteOnly ? 1 : MaxPages;
        _drawPage = (int)Math.Min(v.yoffset / Math.Max(1, v.yres), (uint)(MaxPages - 1));
    }

    /// <summary>Whole pages that fit in both the mapping and the driver's virtual height (at least 1).</summary>
    public static int ComputeMaxPages(nuint mapLen, uint yresVirtual, uint lineLength, uint yres)
    {
        long usable = Math.Min((long)mapLen, (long)yresVirtual * lineLength);
        long pageBytes = (long)lineLength * yres;
        return pageBytes <= 0 ? 1 : (int)Math.Max(1, usable / pageBytes);
    }

    public int Width { get; }
    public int Height { get; }
    public int StrideBytes { get; }
    public PixelLayout Layout { get; }
    public FbPresentMode Mode { get; private set; }
    public int MaxPages { get; }
    public int PageCount { get; private set; }
    public int YOffset => (int)_var.yoffset;
    public bool Degraded { get; private set; }

    public static Framebuffer Open(string path = "/dev/fb0", FbPresentMode mode = FbPresentMode.PanPut)
    {
        int fd = Native.open(path, Native.O_RDWR | Native.O_CLOEXEC);
        if (fd < 0) throw new Win32Exception(Marshal.GetLastPInvokeError(), $"open {path}");
        try
        {
            var v = new FbVarScreenInfo();
            var f = new FbFixScreenInfo();
            if (Native.ioctl(fd, Native.FBIOGET_VSCREENINFO, ref v) != 0 || Native.ioctl(fd, Native.FBIOGET_FSCREENINFO, ref f) != 0)
                throw new Win32Exception(Marshal.GetLastPInvokeError(), "FBIOGET_*SCREENINFO");
            if (v.xres == 0 || v.yres == 0 || f.line_length == 0 || v.bits_per_pixel is < 16 or > 32)
                throw new NotSupportedException($"unsupported fb mode {v.xres}x{v.yres} {v.bits_per_pixel}bpp");
            nint map = Native.mmap(0, f.smem_len, Native.PROT_READ | Native.PROT_WRITE, Native.MAP_SHARED, fd, 0);
            if (map == -1) throw new Win32Exception(Marshal.GetLastPInvokeError(), "mmap");
            var fb = new Framebuffer(fd, map, f.smem_len, v, f, mode);
            // Write-only draws into one page, but a previous run may have left the pan offset elsewhere (we measured
            // yoffset=360 after a page-flipping run). Pan back to page 0 once so what we write is what is visible.
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
        if (PageCount == 1) index = _drawPage; // single-page mode: always the page that is really on screen
        long pageBytes = (long)StrideBytes * Height;
        return new Span<byte>((byte*)_map + index * pageBytes, (int)pageBytes);
    }

    /// <summary>Make <paramref name="index"/> visible. Returns false if the driver refused and we fell back to single-page mode.</summary>
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
        _drawPage = index;
        return true;
    }

    // Try to leave page 0 on screen; returns whether that worked.
    bool ParkOnPageZero(bool force = false)
    {
        if (!force && _var.yoffset == 0) { _drawPage = 0; return true; }
        _var.xoffset = 0;
        _var.yoffset = 0;
        bool ok = Native.ioctl(_fd, Native.FBIOPAN_DISPLAY, ref _var) == 0;
        _var.activate = 0;
        ok &= Native.ioctl(_fd, Native.FBIOPUT_VSCREENINFO, ref _var) == 0;
        if (ok) _drawPage = 0;
        // If even this fails, keep drawing to whichever page the driver last accepted, so the screen at least updates.
        return ok;
    }

    // The driver refused paging mid-flight (e.g. PAN took, PUT failed). Fall back to single page and re-pan to
    // page 0; if that fails too, _drawPage stays on the page we last showed successfully.
    bool Degrade()
    {
        Mode = FbPresentMode.WriteOnly;
        PageCount = 1;
        Degraded = true;
        ParkOnPageZero(force: true);
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
