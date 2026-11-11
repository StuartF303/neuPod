namespace NeuPod.Y2Demo.Ui;

public enum Pattern { ColorBars, Red, Green, Blue, White, Black, GreyRamp, Grid, Checker }

public static class Patterns
{
    public static readonly uint[] BarColors = [0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000];

    public static string Name(Pattern p) => p switch
    {
        Pattern.ColorBars => "Colour bars",
        Pattern.Red => "Red",
        Pattern.Green => "Green",
        Pattern.Blue => "Blue",
        Pattern.White => "White",
        Pattern.Black => "Black",
        Pattern.GreyRamp => "Grey ramp",
        Pattern.Grid => "1 px grid",
        Pattern.Checker => "1 px checker",
        _ => p.ToString(),
    };

    public static void Draw(Canvas c, Pattern p)
    {
        int w = c.Width, h = c.Height;
        switch (p)
        {
            case Pattern.ColorBars:
                for (int i = 0; i < 8; i++) c.FillRect(w * i / 8, 0, w * (i + 1) / 8 - w * i / 8, h, BarColors[i]);
                break;
            case Pattern.Red: c.Clear(0xFF0000); break;
            case Pattern.Green: c.Clear(0x00FF00); break;
            case Pattern.Blue: c.Clear(0x0000FF); break;
            case Pattern.White: c.Clear(0xFFFFFF); break;
            case Pattern.Black: c.Clear(0x000000); break;
            case Pattern.GreyRamp:
                for (int x = 0; x < w; x++)
                {
                    uint g = (uint)(x * 255 / (w - 1));
                    c.FillRect(x, 0, 1, h, g << 16 | g << 8 | g);
                }
                break;
            case Pattern.Grid:
                // 1 px white lines every 16 px plus a border, so the panel edges and any cropping are visible.
                c.Clear(0x000000);
                for (int x = 0; x < w; x += 16) c.FillRect(x, 0, 1, h, 0xFFFFFF);
                for (int y = 0; y < h; y += 16) c.FillRect(0, y, w, 1, 0xFFFFFF);
                c.Rect(0, 0, w, h, 0xFF0000);
                break;
            case Pattern.Checker:
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++)
                        c.Pixels[y * w + x] = ((x ^ y) & 1) == 0 ? 0xFFFFFFu : 0x000000u;
                break;
        }
    }
}
