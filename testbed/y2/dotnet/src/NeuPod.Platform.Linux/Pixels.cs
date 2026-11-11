using System.Runtime.InteropServices;

namespace NeuPod.Platform.Linux;

/// <summary>One colour channel of a framebuffer pixel: bit offset and bit length, as reported by fb_bitfield.</summary>
public readonly record struct ColorChannel(int Offset, int Length);

/// <summary>Pixel format as reported by the framebuffer driver. Never hard-code the layout; use these.</summary>
public readonly record struct PixelLayout(int BitsPerPixel, ColorChannel Red, ColorChannel Green, ColorChannel Blue, ColorChannel Alpha)
{
    public int BytesPerPixel => BitsPerPixel / 8;

    /// <summary>XRGB8888 with the alpha byte exposed as 0xFF (what mtkfb reports on the Y2).</summary>
    public static PixelLayout Xrgb8888 { get; } =
        new(32, new ColorChannel(16, 8), new ColorChannel(8, 8), new ColorChannel(0, 8), new ColorChannel(24, 8));

    public static PixelLayout Rgb565 { get; } =
        new(16, new ColorChannel(11, 5), new ColorChannel(5, 6), new ColorChannel(0, 5), new ColorChannel(0, 0));
}

/// <summary>Converts 0xRRGGBB to the native pixel value using per-channel lookup tables.</summary>
public sealed class PixelPacker
{
    readonly uint[] _r = new uint[256], _g = new uint[256], _b = new uint[256];
    readonly uint _alpha;

    public PixelLayout Layout { get; }

    public PixelPacker(PixelLayout layout)
    {
        if (layout.BitsPerPixel is < 16 or > 32 || layout.BitsPerPixel % 8 != 0)
            throw new NotSupportedException($"{layout.BitsPerPixel} bpp is not supported");
        Layout = layout;
        for (int c = 0; c < 256; c++)
        {
            _r[c] = Scale(c, layout.Red);
            _g[c] = Scale(c, layout.Green);
            _b[c] = Scale(c, layout.Blue);
        }
        // Opaque alpha when the format has one: all-ones in the alpha field.
        _alpha = layout.Alpha.Length == 0 ? 0 : (uint)((1UL << Math.Min(layout.Alpha.Length, 32)) - 1) << layout.Alpha.Offset;
    }

    static uint Scale(int c, ColorChannel ch) =>
        ch.Length == 0 ? 0 : (uint)(c >> (8 - Math.Min(ch.Length, 8))) << ch.Offset;

    public uint Pack(byte r, byte g, byte b) => _r[r] | _g[g] | _b[b] | _alpha;

    public uint Pack(uint rgb) => Pack((byte)(rgb >> 16), (byte)(rgb >> 8), (byte)rgb);
}

/// <summary>A pixel sink: a real framebuffer or an in-memory fake. Pages are stacked vertically.</summary>
public interface IFrameTarget : IDisposable
{
    int Width { get; }
    int Height { get; }
    /// <summary>Bytes per scanline (fb line_length).</summary>
    int StrideBytes { get; }
    /// <summary>Number of full pages usable for double buffering (1 means draw directly to the visible page).</summary>
    int PageCount { get; }
    PixelLayout Layout { get; }
    Span<byte> Page(int index);
    /// <summary>Make the given page the visible one. Returns false if the driver refused.</summary>
    bool Present(int index);
}

public static class FrameBlit
{
    /// <summary>Copy 0xRRGGBB pixels (row-major, <paramref name="srcWidth"/> wide) into a page of the target.</summary>
    public static void Blit(ReadOnlySpan<uint> src, int srcWidth, int srcHeight, IFrameTarget target, int page, PixelPacker packer)
    {
        int w = Math.Min(srcWidth, target.Width), h = Math.Min(srcHeight, target.Height);
        var dst = target.Page(page);
        int bpp = target.Layout.BytesPerPixel;
        for (int y = 0; y < h; y++)
        {
            var srow = src.Slice(y * srcWidth, w);
            var drow = dst.Slice(y * target.StrideBytes, w * bpp);
            switch (bpp)
            {
                case 4:
                    var d32 = MemoryMarshal.Cast<byte, uint>(drow);
                    for (int x = 0; x < w; x++) d32[x] = packer.Pack(srow[x]);
                    break;
                case 3:
                    for (int x = 0; x < w; x++)
                    {
                        uint p = packer.Pack(srow[x]);
                        drow[x * 3] = (byte)p; drow[x * 3 + 1] = (byte)(p >> 8); drow[x * 3 + 2] = (byte)(p >> 16);
                    }
                    break;
                default:
                    var d16 = MemoryMarshal.Cast<byte, ushort>(drow);
                    for (int x = 0; x < w; x++) d16[x] = (ushort)packer.Pack(srow[x]);
                    break;
            }
        }
    }

    /// <summary>Fill the whole page with one colour.</summary>
    public static void Fill(IFrameTarget target, int page, uint rgb, PixelPacker packer)
    {
        var px = new uint[target.Width * target.Height];
        Array.Fill(px, rgb);
        Blit(px, target.Width, target.Height, target, page, packer);
    }
}

/// <summary>In-memory frame target for host tests and tools. Records the last presented page.</summary>
public sealed class InMemoryFrameTarget : IFrameTarget
{
    readonly byte[] _mem;

    public InMemoryFrameTarget(int width, int height, int pages, PixelLayout layout)
    {
        Width = width; Height = height; PageCount = pages; Layout = layout;
        StrideBytes = width * layout.BytesPerPixel;
        _mem = new byte[StrideBytes * height * pages];
    }

    public int Width { get; }
    public int Height { get; }
    public int StrideBytes { get; }
    public int PageCount { get; }
    public PixelLayout Layout { get; }
    public int VisiblePage { get; private set; }
    public int PresentCount { get; private set; }

    public Span<byte> Page(int index) => _mem.AsSpan(index * StrideBytes * Height, StrideBytes * Height);

    public bool Present(int index)
    {
        VisiblePage = index;
        PresentCount++;
        return true;
    }

    public void Dispose() { }
}
