using System.Runtime.InteropServices;

namespace NeuPod.Platform.Linux;

/// <summary>
/// fb_var_screeninfo. 160 bytes on every Linux ABI (all fields are __u32), so it is safe on armhf and on
/// the 64-bit host alike.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
internal struct FbVarScreenInfo
{
    public uint xres, yres, xres_virtual, yres_virtual, xoffset, yoffset, bits_per_pixel, grayscale;
    public FbBitfield red, green, blue, transp;
    public uint nonstd, activate, height, width, accel_flags;
    public uint pixclock, left_margin, right_margin, upper_margin, lower_margin, hsync_len, vsync_len;
    public uint sync, vmode, rotate, colorspace;
    public uint reserved0, reserved1, reserved2, reserved3;
}

[StructLayout(LayoutKind.Sequential)]
internal struct FbBitfield
{
    public uint offset, length, msb_right;
}

/// <summary>
/// fb_fix_screeninfo. Contains two <c>unsigned long</c> fields (smem_start, mmio_start), so the size follows
/// the pointer width: 68 bytes on armhf (the device), 80 on 64-bit. <see cref="nuint"/> tracks that.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct FbFixScreenInfo
{
    public fixed byte id[16];
    public nuint smem_start;
    public uint smem_len, type, type_aux, visual;
    public ushort xpanstep, ypanstep, ywrapstep;
    public uint line_length;
    public nuint mmio_start;
    public uint mmio_len, accel;
    public ushort capabilities, reserved0, reserved1;
}

internal static unsafe class Native
{
    const string Libc = "libc.so.6";

    public const int O_RDONLY = 0, O_RDWR = 2, O_CLOEXEC = 0x80000;
    public const int PROT_READ = 1, PROT_WRITE = 2, MAP_SHARED = 1;
    public const short POLLIN = 1;
    public const uint FBIOGET_VSCREENINFO = 0x4600, FBIOPUT_VSCREENINFO = 0x4601,
        FBIOGET_FSCREENINFO = 0x4602, FBIOPAN_DISPLAY = 0x4606;

    [StructLayout(LayoutKind.Sequential)]
    public struct PollFd
    {
        public int fd;
        public short events, revents;
    }

    [DllImport(Libc, SetLastError = true)]
    public static extern int open([MarshalAs(UnmanagedType.LPStr)] string path, int flags);

    [DllImport(Libc, SetLastError = true)]
    public static extern int close(int fd);

    [DllImport(Libc, SetLastError = true)]
    public static extern int ioctl(int fd, nuint request, ref FbVarScreenInfo arg);

    [DllImport(Libc, SetLastError = true)]
    public static extern int ioctl(int fd, nuint request, ref FbFixScreenInfo arg);

    // off_t is 32-bit for the plain mmap symbol on armhf; we only ever map at offset 0.
    [DllImport(Libc, SetLastError = true)]
    public static extern nint mmap(nint addr, nuint length, int prot, int flags, int fd, nint offset);

    [DllImport(Libc, SetLastError = true)]
    public static extern int munmap(nint addr, nuint length);

    [DllImport(Libc, SetLastError = true)]
    public static extern nint read(int fd, byte* buf, nuint count);

    [DllImport(Libc, SetLastError = true)]
    public static extern int poll(PollFd* fds, nuint nfds, int timeoutMs);
}
