using System.Globalization;
using NeuPod.Platform.Linux;

namespace NeuPod.Y2Demo.Ui;

/// <summary>Draws an <see cref="AppModel"/> into a <see cref="Canvas"/>. Pure function of the model: no I/O, no timers.</summary>
public sealed class Renderer(BdfFont small, BdfFont large)
{
    public const int Width = 480, Height = 360, TitleHeight = 32, RowHeight = 28;

    const uint Bg = 0xFFFFFF, Ink = 0x101010, Dim = 0x707070, Accent = 0x1F6FD0, AccentLight = 0x4AA3F0;
    const uint BarTop = 0xF4F4F4, BarBottom = 0xBBBBBB, BarLine = 0x808080;

    public BdfFont Small { get; } = small;
    public BdfFont Large { get; } = large;

    public static Renderer CreateDefault() =>
        new(BdfFont.LoadEmbedded("spleen-8x16"), BdfFont.LoadEmbedded("spleen-12x24"));

    public void Render(AppModel m, Canvas c)
    {
        if (m.Screen == ScreenId.DisplayTest)
        {
            Patterns.Draw(c, m.Pattern);
            if (m.ShowOverlay) DisplayOverlay(m, c);
            return;
        }
        c.Clear(Bg);
        TitleBar(m, c);
        switch (m.Screen)
        {
            case ScreenId.Main: MainMenu(m, c); break;
            case ScreenId.InputTest: InputTest(m, c); break;
            case ScreenId.Backlight: BacklightScreen(m, c); break;
            case ScreenId.System: SystemScreen(m, c); break;
            case ScreenId.About: About(m, c); break;
        }
    }

    // ---- title bar ----

    void TitleBar(AppModel m, Canvas c)
    {
        c.VGradient(0, 0, Width, TitleHeight, BarTop, BarBottom);
        c.FillRect(0, TitleHeight - 1, Width, 1, BarLine);

        int tx = (Width - c.TextWidth(Large, m.Title)) / 2;
        c.Text(Large, tx, (TitleHeight - Large.CellHeight) / 2, m.Title, Ink);

        string clock = m.Clock.LocalNow.ToString("HH:mm", CultureInfo.InvariantCulture);
        c.Text(Small, Width - 6 - c.TextWidth(Small, clock), (TitleHeight - Small.CellHeight) / 2, clock, Ink);

        // Left side: battery percent (or n/a), then a headphone marker when something is plugged in.
        int x = 6;
        if (m.Info.BatteryPercent is { } pct)
        {
            BatteryIcon(c, x, 10, pct);
            x += 30;
            string t = pct.ToString(CultureInfo.InvariantCulture) + "%";
            c.Text(Small, x, (TitleHeight - Small.CellHeight) / 2, t, Ink);
            x += c.TextWidth(Small, t) + 6;
        }
        else
        {
            c.Text(Small, x, (TitleHeight - Small.CellHeight) / 2, "n/a", Dim);
            x += c.TextWidth(Small, "n/a") + 6;
        }
        if (m.Info.Jack is JackState.Headphones or JackState.Headset) HeadphoneIcon(c, x, 8);
    }

    static void BatteryIcon(Canvas c, int x, int y, int pct)
    {
        c.Rect(x, y, 24, 12, Ink);
        c.FillRect(x + 24, y + 3, 2, 6, Ink);
        int fill = Math.Clamp(pct, 0, 100) * 20 / 100;
        uint col = pct <= 15 ? 0xD02020u : 0x20A020u;
        c.FillRect(x + 2, y + 2, fill, 8, col);
    }

    static void HeadphoneIcon(Canvas c, int x, int y)
    {
        // Band (arch) plus two ear cups: cheap but recognisable at 16 px.
        for (int i = 2; i < 14; i++) c.Set(x + i, y + (i is 2 or 13 ? 4 : i is 3 or 12 ? 2 : i is 4 or 11 ? 1 : 0), Ink);
        c.FillRect(x + 1, y + 5, 3, 9, Ink);
        c.FillRect(x + 12, y + 5, 3, 9, Ink);
        c.FillRect(x + 1, y + 3, 1, 3, Ink);
        c.FillRect(x + 14, y + 3, 1, 3, Ink);
    }

    // ---- main menu ----

    void MainMenu(AppModel m, Canvas c)
    {
        var list = m.Menu;
        for (int i = 0; i < list.VisibleRows; i++)
        {
            int idx = list.Top + i;
            if (idx >= list.Count) break;
            int y = TitleHeight + i * RowHeight;
            bool sel = idx == list.Selected;
            if (sel) c.VGradient(0, y, Width, RowHeight, AccentLight, Accent);
            uint fg = sel ? 0xFFFFFF : Ink;
            c.Text(Large, 14, y + (RowHeight - Large.CellHeight) / 2, AppModel.MenuItems[idx].Title, fg);
            c.Text(Large, Width - 14 - Large.CellWidth, y + (RowHeight - Large.CellHeight) / 2, ">", sel ? 0xFFFFFF : Dim);
        }
        // Scroll marker when the list is longer than the window.
        if (list.Count > list.VisibleRows)
        {
            int trackH = list.VisibleRows * RowHeight;
            int thumbH = Math.Max(8, trackH * list.VisibleRows / list.Count);
            int thumbY = TitleHeight + (trackH - thumbH) * list.Top / (list.Count - list.VisibleRows);
            c.FillRect(Width - 4, thumbY, 3, thumbH, Dim);
        }
    }

    // ---- input test ----

    void InputTest(AppModel m, Canvas c)
    {
        int lh = Small.CellHeight + 2, y = TitleHeight + 6;
        c.Text(Small, 8, y, "Last events (name, action, ms since previous)", Dim);
        y += lh + 2;
        var log = m.InputLog;
        for (int i = 0; i < AppModel.InputLogSize; i++)
        {
            int rowY = y + i * lh;
            if (i < log.Count)
            {
                var e = log[log.Count - 1 - i]; // newest first
                uint fg = i == 0 ? Accent : Ink;
                c.Text(Small, 8, rowY, e.Name.PadRight(15), fg);
                c.Text(Small, 8 + 16 * 8, rowY, e.Action.PadRight(8), fg);
                c.Text(Small, 8 + 24 * 8, rowY, "+" + e.DeltaMs.ToString(CultureInfo.InvariantCulture) + " ms", fg);
            }
            else c.Text(Small, 8, rowY, "-", 0xC0C0C0);
        }
        y += AppModel.InputLogSize * lh + 6;

        c.FillRect(8, y - 3, Width - 16, 1, 0xC0C0C0);
        long now = m.Clock.NowMs;
        c.Text(Small, 8, y, "Wheel ticks/s: " + m.WheelTicksPerSecond(now).ToString(CultureInfo.InvariantCulture), Ink);
        y += lh + 2;

        // Counters in three columns, stable order.
        var keys = m.Counters.Keys.OrderBy(k => k, StringComparer.Ordinal).ToList();
        for (int i = 0; i < keys.Count && i < 12; i++)
        {
            int col = i / 4, row = i % 4;
            string k = keys[i].Replace("KEY_", "", StringComparison.Ordinal);
            string txt = k + " " + m.Counters[keys[i]].ToString(CultureInfo.InvariantCulture);
            c.Text(Small, 8 + col * 156, y + row * lh, txt, Ink);
        }

        // Hold-to-exit progress along the bottom.
        int by = Height - 22;
        if (m.BackHeld)
        {
            int w = (int)(Width * Math.Min(1.0, (double)m.InputHoldMs / AppModel.MenuHoldMs));
            c.FillRect(0, by - 6, w, 4, Accent);
        }
        c.Text(Small, 8, by, "Hold MENU 1 s to leave", Dim);
    }

    // ---- display test overlay ----

    void DisplayOverlay(AppModel m, Canvas c)
    {
        string fps = m.FlipFps is { } f ? f.ToString("0.0", CultureInfo.InvariantCulture) + " fps" : "measuring...";
        string[] lines =
        [
            $"{Patterns.Name(m.Pattern)}  {(int)m.Pattern + 1}/{Enum.GetValues<Pattern>().Length}",
            "flip " + fps,
            "wheel pattern  PLAY overlay",
            "CENTRE re-measure  MENU exit",
        ];
        int w = lines.Max(l => l.Length) * Small.CellWidth + 12, h = lines.Length * (Small.CellHeight + 2) + 8;
        c.FillRect(8, 8, w, h, 0x000000);
        c.Rect(8, 8, w, h, 0xFFFFFF);
        for (int i = 0; i < lines.Length; i++)
            c.Text(Small, 14, 12 + i * (Small.CellHeight + 2), lines[i], 0xFFFFFF);
    }

    // ---- backlight ----

    void BacklightScreen(AppModel m, Canvas c)
    {
        int v = m.BacklightValue;
        string label = $"Brightness {v * 100 / 255}%  ({v}/255)";
        c.Text(Large, (Width - c.TextWidth(Large, label)) / 2, 100, label, Ink);

        const int bx = 40, bw = Width - 80, bh = 28, by = 160;
        c.Rect(bx, by, bw, bh, Ink);
        c.FillRect(bx + 2, by + 2, bw - 4, bh - 4, 0xE8E8E8);
        int fill = (bw - 4) * v / 255;
        c.VGradient(bx + 2, by + 2, fill, bh - 4, AccentLight, Accent);
        // Mark the clamp floor so it is obvious why the wheel stops.
        int floorX = bx + 2 + (bw - 4) * Platform.Linux.Backlight.Min / 255;
        c.FillRect(floorX, by - 4, 1, bh + 8, 0xD02020);

        c.Text(Small, (Width - c.TextWidth(Small, "wheel: adjust   Vol+/Vol-: adjust anywhere")) / 2, 220, "wheel: adjust   Vol+/Vol-: adjust anywhere", Dim);
        c.Text(Small, (Width - c.TextWidth(Small, "saved when you leave this screen")) / 2, 240, "saved when you leave this screen", Dim);
        c.Text(Small, (Width - c.TextWidth(Small, "minimum 8 so the screen never goes black")) / 2, 260, "minimum 8 so the screen never goes black", Dim);
    }

    // ---- system ----

    void SystemScreen(AppModel m, Canvas c)
    {
        var s = m.Info;
        string na = "n/a";
        string Fmt(double? d) => d is { } v ? v.ToString("0.00", CultureInfo.InvariantCulture) : na;

        string uptime = s.Uptime is { } u
            ? $"{(int)u.TotalDays}d {u.Hours:00}:{u.Minutes:00}:{u.Seconds:00}" : na;
        string mem = s.MemTotalKb is { } t && s.MemAvailableKb is { } a
            ? $"{a / 1024} / {t / 1024} MB free" : na;
        string batt = s.BatteryPercent is { } p
            ? $"{p}%" + (s.BatteryMillivolts is { } mv ? $" {mv} mV" : "") + (s.BatteryStatus is { Length: > 0 } st ? " " + st : "") : na;
        string jack = s.Jack switch
        {
            JackState.None => "none",
            JackState.Headphones => "headphones",
            JackState.Headset => "headset (mic)",
            _ => na,
        };

        (string, string)[] rows =
        [
            ("Uptime", uptime),
            ("Load", $"{Fmt(s.Load1)} {Fmt(s.Load5)} {Fmt(s.Load15)}"),
            ("Memory", mem),
            ("CPUs online", s.OnlineCpus?.ToString(CultureInfo.InvariantCulture) ?? na),
            ("Kernel", s.Kernel ?? na),
            ("IPv4", s.Ipv4 ?? na),
            ("Battery", batt),
            ("Jack", jack),
            (".NET", m.RuntimeVersion),
        ];
        int y = TitleHeight + 10, lh = Small.CellHeight + 8;
        foreach (var (k, v) in rows)
        {
            c.Text(Small, 12, y, k, Dim);
            c.Text(Small, 12 + 14 * 8, y, v, Ink);
            c.FillRect(12, y + lh - 5, Width - 24, 1, 0xE0E0E0);
            y += lh;
        }
        c.Text(Small, 12, Height - 22, "refreshes every second", Dim);
    }

    // ---- about ----

    void About(AppModel m, Canvas c)
    {
        int y = TitleHeight + 30;
        c.Text(Large, 24, y, "neuPod Y2 demo", Ink);
        y += 44;
        c.Text(Small, 24, y, "Build   " + m.GitHash, Ink); y += 24;
        c.Text(Small, 24, y, "Runtime " + m.RuntimeVersion, Ink); y += 24;
        c.Text(Small, 24, y, "Font    Spleen 8x16, 12x24 (BSD-2-Clause)", Ink); y += 40;
        c.Text(Large, 24, y, "Testbed, not shipping.", Accent);
    }
}
