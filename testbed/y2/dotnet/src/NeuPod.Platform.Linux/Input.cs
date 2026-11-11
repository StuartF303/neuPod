using System.Buffers.Binary;

namespace NeuPod.Platform.Linux;

/// <summary>Linux input key codes used by the Y2 (see testbed/y2/docs/y2-input-map.md).</summary>
public static class Keys
{
    public const ushort Enter = 28, WheelUp = 103, Left = 105, Right = 106, WheelDown = 108,
        VolumeDown = 114, VolumeUp = 115, Power = 116, Back = 158, PlayPause = 164;

    public static string Name(ushort code) => code switch
    {
        Enter => "KEY_ENTER",
        WheelUp => "KEY_UP",
        Left => "KEY_LEFT",
        Right => "KEY_RIGHT",
        WheelDown => "KEY_DOWN",
        VolumeDown => "KEY_VOLUMEDOWN",
        VolumeUp => "KEY_VOLUMEUP",
        Power => "KEY_POWER",
        Back => "KEY_BACK",
        PlayPause => "KEY_PLAYPAUSE",
        _ => $"KEY_{code}",
    };

    /// <summary>Ring buttons on the click wheel (not the wheel rotation itself).</summary>
    public static bool IsRingButton(ushort code) => code is Enter or Back or Left or Right or PlayPause;
}

/// <summary>Raw 16-byte 32-bit struct input_event.</summary>
public readonly record struct RawInputEvent(int Seconds, int Microseconds, ushort Type, ushort Code, int Value)
{
    public const int Size = 16;
    public const ushort EvKey = 1;
}

public enum InputKind { KeyPress, KeyRelease, KeyRepeat, WheelCw, WheelCcw }

/// <summary>A typed input event. <see cref="TimeMs"/> is monotonic milliseconds taken when the event was read.</summary>
public readonly record struct InputEvent(InputKind Kind, ushort Code, long TimeMs)
{
    public bool IsWheel => Kind is InputKind.WheelCw or InputKind.WheelCcw;
}

/// <summary>Reassembles 16-byte input_event records from arbitrary read chunks.</summary>
public sealed class InputEventDecoder
{
    readonly byte[] _carry = new byte[RawInputEvent.Size];
    int _carried;

    /// <summary>Bytes held back waiting for the rest of a record.</summary>
    public int Pending => _carried;

    public void Feed(ReadOnlySpan<byte> data, List<RawInputEvent> output)
    {
        while (!data.IsEmpty)
        {
            if (_carried > 0 || data.Length < RawInputEvent.Size)
            {
                int take = Math.Min(RawInputEvent.Size - _carried, data.Length);
                data[..take].CopyTo(_carry.AsSpan(_carried));
                _carried += take;
                data = data[take..];
                if (_carried < RawInputEvent.Size) return;
                output.Add(Decode(_carry));
                _carried = 0;
            }
            else
            {
                output.Add(Decode(data[..RawInputEvent.Size]));
                data = data[RawInputEvent.Size..];
            }
        }
    }

    public static RawInputEvent Decode(ReadOnlySpan<byte> r) => new(
        BinaryPrimitives.ReadInt32LittleEndian(r),
        BinaryPrimitives.ReadInt32LittleEndian(r[4..]),
        BinaryPrimitives.ReadUInt16LittleEndian(r[8..]),
        BinaryPrimitives.ReadUInt16LittleEndian(r[10..]),
        BinaryPrimitives.ReadInt32LittleEndian(r[12..]));
}

public static class InputMapper
{
    /// <summary>
    /// Turn a raw key event into a typed one. The wheel reports press+release per detent; we emit one tick on
    /// the press and drop the release. Non-key events (EV_SYN etc.) map to null.
    /// </summary>
    public static InputEvent? Map(RawInputEvent e, long nowMs)
    {
        if (e.Type != RawInputEvent.EvKey) return null;
        if (e.Code is Keys.WheelDown or Keys.WheelUp)
        {
            if (e.Value != 1) return null;
            return new InputEvent(e.Code == Keys.WheelDown ? InputKind.WheelCw : InputKind.WheelCcw, e.Code, nowMs);
        }
        return e.Value switch
        {
            0 => new InputEvent(InputKind.KeyRelease, e.Code, nowMs),
            1 => new InputEvent(InputKind.KeyPress, e.Code, nowMs),
            2 => new InputEvent(InputKind.KeyRepeat, e.Code, nowMs),
            _ => null,
        };
    }
}

/// <summary>
/// Drops wheel ticks that arrive shortly after a ring-button press: a finger brushing the wheel on the way
/// to a ring button fires a stray KEY_DOWN. Not thread-safe; EvdevInput serialises access.
/// </summary>
public sealed class RingDebounce(long windowMs = 150)
{
    long _lastRingPress = long.MinValue / 2;

    public long WindowMs { get; } = windowMs;

    public bool Accept(InputEvent e)
    {
        if (e.Kind == InputKind.KeyPress && Keys.IsRingButton(e.Code))
        {
            _lastRingPress = e.TimeMs;
            return true;
        }
        if (e.IsWheel && e.TimeMs - _lastRingPress < WindowMs) return false;
        return true;
    }
}
