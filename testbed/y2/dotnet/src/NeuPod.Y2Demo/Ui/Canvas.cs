namespace NeuPod.Y2Demo.Ui;

/// <summary>Managed 0xRRGGBB back buffer with a handful of drawing primitives.</summary>
public sealed class Canvas(int width, int height)
{
    public int Width { get; } = width;
    public int Height { get; } = height;
    public uint[] Pixels { get; } = new uint[width * height];

    public void Clear(uint rgb) => Array.Fill(Pixels, rgb & 0xFFFFFF);

    public void Set(int x, int y, uint rgb)
    {
        if ((uint)x < (uint)Width && (uint)y < (uint)Height) Pixels[y * Width + x] = rgb & 0xFFFFFF;
    }

    public uint Get(int x, int y) => Pixels[y * Width + x];

    public void FillRect(int x, int y, int w, int h, uint rgb)
    {
        int x0 = Math.Max(0, x), y0 = Math.Max(0, y), x1 = Math.Min(Width, x + w), y1 = Math.Min(Height, y + h);
        if (x1 <= x0) return;
        for (int yy = y0; yy < y1; yy++)
            Pixels.AsSpan(yy * Width + x0, x1 - x0).Fill(rgb & 0xFFFFFF);
    }

    public void Rect(int x, int y, int w, int h, uint rgb)
    {
        FillRect(x, y, w, 1, rgb);
        FillRect(x, y + h - 1, w, 1, rgb);
        FillRect(x, y, 1, h, rgb);
        FillRect(x + w - 1, y, 1, h, rgb);
    }

    /// <summary>Vertical gradient between two colours.</summary>
    public void VGradient(int x, int y, int w, int h, uint top, uint bottom)
    {
        for (int i = 0; i < h; i++)
            FillRect(x, y + i, w, 1, Lerp(top, bottom, h <= 1 ? 0 : (double)i / (h - 1)));
    }

    public static uint Lerp(uint a, uint b, double t)
    {
        static uint Ch(uint a, uint b, int sh, double t) => (uint)Math.Round(((a >> sh) & 0xFF) * (1 - t) + ((b >> sh) & 0xFF) * t) & 0xFF;
        return Ch(a, b, 16, t) << 16 | Ch(a, b, 8, t) << 8 | Ch(a, b, 0, t);
    }

    /// <summary>Draw text with the top-left of the first cell at (x, y). Returns the x after the last glyph.</summary>
    public int Text(BdfFont font, int x, int y, string text, uint fg, uint? bg = null)
    {
        foreach (char ch in text)
        {
            var g = font.Glyph(ch);
            if (bg is { } b) FillRect(x, y, font.CellWidth, font.CellHeight, b);
            int baseline = y + font.Ascent;
            for (int row = 0; row < g.Height; row++)
            {
                int py = baseline - (g.YOffset + g.Height) + row;
                uint bits = g.Rows[row];
                for (int col = 0; col < g.Width; col++)
                    if ((bits >> (g.RowBits - 1 - col) & 1) != 0) Set(x + g.XOffset + col, py, fg);
            }
            x += font.CellWidth;
        }
        return x;
    }

    public int TextWidth(BdfFont font, string text) => text.Length * font.CellWidth;
}
