using System.Buffers.Binary;
using System.IO.Compression;
using NeuPod.Y2Demo.Ui;

namespace NeuPod.Y2Demo.Host;

/// <summary>Minimal 8-bit RGB PNG encoder (filter 0, zlib via System.IO.Compression).</summary>
public static class Png
{
    static readonly uint[] CrcTable = BuildCrcTable();

    static uint[] BuildCrcTable()
    {
        var t = new uint[256];
        for (uint n = 0; n < 256; n++)
        {
            uint c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320 ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }

    public static uint Crc32(ReadOnlySpan<byte> data, uint crc = 0)
    {
        crc ^= 0xFFFFFFFF;
        foreach (byte b in data) crc = CrcTable[(crc ^ b) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFF;
    }

    public static byte[] Encode(Canvas c) => Encode(c.Pixels, c.Width, c.Height);

    public static byte[] Encode(ReadOnlySpan<uint> rgb, int width, int height)
    {
        var raw = new byte[(width * 3 + 1) * height];
        for (int y = 0; y < height; y++)
        {
            int o = y * (width * 3 + 1);
            raw[o] = 0;
            for (int x = 0; x < width; x++)
            {
                uint p = rgb[y * width + x];
                raw[o + 1 + x * 3] = (byte)(p >> 16);
                raw[o + 2 + x * 3] = (byte)(p >> 8);
                raw[o + 3 + x * 3] = (byte)p;
            }
        }
        using var z = new MemoryStream();
        using (var zs = new ZLibStream(z, CompressionLevel.Optimal, leaveOpen: true)) zs.Write(raw);

        using var png = new MemoryStream();
        png.Write([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]);
        Span<byte> ihdr = stackalloc byte[13];
        BinaryPrimitives.WriteUInt32BigEndian(ihdr, (uint)width);
        BinaryPrimitives.WriteUInt32BigEndian(ihdr[4..], (uint)height);
        ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0; // 8-bit truecolour
        Chunk(png, "IHDR"u8, ihdr);
        Chunk(png, "IDAT"u8, z.GetBuffer().AsSpan(0, (int)z.Length));
        Chunk(png, "IEND"u8, []);
        return png.ToArray();
    }

    static void Chunk(Stream s, ReadOnlySpan<byte> type, ReadOnlySpan<byte> data)
    {
        Span<byte> len = stackalloc byte[4];
        BinaryPrimitives.WriteUInt32BigEndian(len, (uint)data.Length);
        s.Write(len);
        s.Write(type);
        s.Write(data);
        uint crc = Crc32(data, Crc32(type));
        BinaryPrimitives.WriteUInt32BigEndian(len, crc);
        s.Write(len);
    }
}
