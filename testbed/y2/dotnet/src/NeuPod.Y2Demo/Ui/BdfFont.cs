using System.Globalization;
using System.Reflection;

namespace NeuPod.Y2Demo.Ui;

public sealed record Glyph(int Width, int Height, int XOffset, int YOffset, uint[] Rows, int RowBits);

/// <summary>Monospace BDF bitmap font parsed at runtime (Spleen, BSD-2-Clause; see Fonts/LICENSE-spleen.txt).</summary>
public sealed class BdfFont
{
    readonly Dictionary<int, Glyph> _glyphs = [];
    readonly Glyph _fallback;

    BdfFont(int cellW, int cellH, int ascent, int defaultChar)
    {
        CellWidth = cellW; CellHeight = cellH; Ascent = ascent; DefaultChar = defaultChar;
        _fallback = new Glyph(0, 0, 0, 0, [], 8);
    }

    public int CellWidth { get; }
    public int CellHeight { get; }
    public int Ascent { get; }
    public int DefaultChar { get; }
    public int GlyphCount => _glyphs.Count;

    public bool Has(int codepoint) => _glyphs.ContainsKey(codepoint);

    public Glyph Glyph(char c) =>
        _glyphs.TryGetValue(c, out var g) ? g
        : _glyphs.TryGetValue(DefaultChar, out var d) ? d : _fallback;

    static readonly Assembly Asm = typeof(BdfFont).Assembly;

    /// <summary>Load an embedded font, e.g. "spleen-8x16".</summary>
    public static BdfFont LoadEmbedded(string name)
    {
        using var s = Asm.GetManifestResourceStream($"Fonts.{name}.bdf")
            ?? throw new FileNotFoundException($"embedded font {name} not found");
        using var r = new StreamReader(s);
        return Parse(r.ReadToEnd());
    }

    public static BdfFont Parse(string bdf)
    {
        int cw = 0, ch = 0, ascent = 0, defChar = 32;
        BdfFont? font = null;
        int enc = -1, bw = 0, bh = 0, bx = 0, by = 0;
        List<uint>? rows = null;
        foreach (var raw in bdf.Split('\n'))
        {
            var line = raw.Trim();
            if (rows is not null && line != "ENDCHAR")
            {
                if (line == "BITMAP") continue;
                if (rows.Count < bh && line.Length > 0)
                    rows.Add(uint.Parse(line, NumberStyles.HexNumber, CultureInfo.InvariantCulture));
                continue;
            }
            var p = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            if (p.Length == 0) continue;
            switch (p[0])
            {
                case "FONTBOUNDINGBOX": cw = int.Parse(p[1]); ch = int.Parse(p[2]); break;
                case "FONT_ASCENT": ascent = int.Parse(p[1]); break;
                case "DEFAULT_CHAR": defChar = int.Parse(p[1]); break;
                case "STARTCHAR": font ??= new BdfFont(cw, ch, ascent, defChar); enc = -1; break;
                case "ENCODING": enc = int.Parse(p[1]); break;
                case "BBX": bw = int.Parse(p[1]); bh = int.Parse(p[2]); bx = int.Parse(p[3]); by = int.Parse(p[4]); break;
                case "BITMAP": rows = []; break;
                case "ENDCHAR":
                    if (font is not null && rows is not null && enc >= 0)
                    {
                        // BDF rows are left-aligned in whole bytes; keep that bit width so Canvas can index from the MSB.
                        int bytes = (bw + 7) / 8;
                        font._glyphs[enc] = new Glyph(bw, bh, bx, by, rows.ToArray(), bytes * 8);
                    }
                    rows = null;
                    break;
            }
        }
        if (font is null) throw new FormatException("no glyphs in BDF");
        return font;
    }
}
