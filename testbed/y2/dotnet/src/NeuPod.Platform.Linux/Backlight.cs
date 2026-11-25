namespace NeuPod.Platform.Linux;

public interface IBacklight
{
    int Value { get; }
    void Set(int value);
}

/// <summary>sysfs LED backlight (0-255). The minimum is clamped so the panel never goes fully black.</summary>
public sealed class Backlight : IBacklight
{
    public const string DefaultPath = "/sys/class/leds/lcd-backlight/brightness";
    public const int Min = 8, Max = 255;

    readonly string _path;
    int _value;

    public Backlight(string path = DefaultPath)
    {
        _path = path;
        _value = TryRead() ?? 128;
    }

    public static int Clamp(int v) => Math.Clamp(v, Min, Max);

    public bool Available => File.Exists(_path);

    public int Value => _value;

    public void Set(int value)
    {
        _value = Clamp(value);
        try { File.WriteAllText(_path, _value.ToString(System.Globalization.CultureInfo.InvariantCulture)); }
        catch (IOException) { /* sysfs write failed; keep the in-memory value */ }
        catch (UnauthorizedAccessException) { }
    }

    int? TryRead()
    {
        try { return int.TryParse(File.ReadAllText(_path).Trim(), out int v) ? Clamp(v) : null; }
        catch (IOException) { return null; }
        catch (UnauthorizedAccessException) { return null; }
    }
}
