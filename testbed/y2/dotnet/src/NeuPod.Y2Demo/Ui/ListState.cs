namespace NeuPod.Y2Demo.Ui;

/// <summary>Selection and scroll window for a vertical list. The wheel clamps at the ends like an iPod.</summary>
public sealed class ListState(int count, int visibleRows)
{
    public int Count { get; } = count;
    public int VisibleRows { get; } = visibleRows;
    public int Selected { get; private set; }
    public int Top { get; private set; }

    /// <summary>Move by delta rows. Returns true if the selection changed.</summary>
    public bool Move(int delta)
    {
        int next = Math.Clamp(Selected + delta, 0, Math.Max(0, Count - 1));
        if (next == Selected) return false;
        Selected = next;
        if (Selected < Top) Top = Selected;
        else if (Selected >= Top + VisibleRows) Top = Selected - VisibleRows + 1;
        return true;
    }
}
