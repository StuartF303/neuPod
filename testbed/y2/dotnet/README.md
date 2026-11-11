# Y2 .NET demo

Small interactive C# (.NET 10) demo for the Innioasis Y2 testbed (MT6582, ARMv7, stock 3.4.67 kernel, Buildroot glibc
rootfs). It is the seed for stage 3 (a neuPod UI on `NeuPod.Platform.Linux`): the platform layer is reusable and
knows nothing about the UI.

```
src/NeuPod.Platform.Linux/   Framebuffer (mmap) + in-memory fake, EvdevInput, Backlight, SysInfo
src/NeuPod.Y2Demo/           the app: model (navigation), renderer (software, 480x360), BDF font, PNG writer
tests/NeuPod.Y2.Tests/       xUnit, no device or real ioctls needed
NeuPod.Y2.slnx
```

## Build, test

```
dotnet test testbed/y2/dotnet/NeuPod.Y2.slnx
```

No NuGet packages beyond xUnit and the test SDK. Nullable and warnings-as-errors are on.

Review the screens on the host without a framebuffer (fake clock, system info and input):

```
dotnet run --project testbed/y2/dotnet/src/NeuPod.Y2Demo -- --render-png /tmp/y2shots
```

## Deploy

```
testbed/y2/scripts/deploy-demo.sh [--no-restart] [--dry-run]     # Y2_HOST=root@192.168.7.2 by default
```

Publishes self-contained `linux-arm` Release, streams it over ssh into `/opt/neupod-demo.new`, then renames it to
`/opt/neupod-demo` (a failed copy never replaces a working install), installs `/etc/init.d/S90neupod-demo`, and
restarts it. It touches nothing else (the app's own `/etc/neupod-demo.conf` aside). The init script is also in the
rootfs overlay, so future rootfs builds include it; it does nothing if `/opt/neupod-demo/NeuPod.Y2Demo` is missing.

On the device: log at `/tmp/neupod-demo.log` (tmpfs), PID files `/run/neupod-demo.pid` (respawn wrapper) and
`/run/neupod-demo.app.pid` (app). The wrapper restarts a crashed app after 2 s. `stop` kills by PID, never by name;
SIGTERM/SIGINT make the app paint the screen dark grey (0x202020) and exit 0.

If the deploy refuses to connect with "REMOTE HOST IDENTIFICATION HAS CHANGED", the Y2's ssh host key was regenerated
(normal after a reflash or new rootfs). Check it is your device, run `ssh-keygen -R <host>`, connect once to accept the new
key, and re-run. The script never disables host-key checking.

`--no-restart` only stages: the new build is copied to `/opt/neupod-demo.new` and the init script refreshed, but the live
install is not swapped (replacing files under a running .NET process can crash it on a lazy assembly load). A normal run
stops the app, swaps, starts it, checks it stays up for 4 s, and otherwise restores the previous build.

The init script exports `DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1` and `DOTNET_EnableWriteXorExecute=0` itself, because
`/etc/profile.d/dotnet.sh` only covers login shells. The csproj also sets InvariantGlobalization (no ICU on device).

## Controls

| Control | Effect |
| --- | --- |
| Wheel | move highlight / change value / cycle pattern |
| Centre | enter |
| Menu | back (in Input test: hold 1 s) |
| Vol+ / Vol- | backlight +/-16 from any screen (minimum 8) |
| Play/Pause | Display test: toggle the overlay |
| Centre (Display test) | re-measure flip fps |

Screens: Input test (last 8 events with ms deltas, per-key counters, wheel ticks/s), Display test (colour bars, R/G/B/W/K,
grey ramp, 1 px grid, 1 px checker, with measured fps), Backlight (bar, wheel adjusts, floor at 8), System (1 Hz: uptime,
load, memory, CPUs, kernel, IPv4 of rndis0/usb0, battery, jack, .NET), About (git hash, "testbed, not shipping").

Backlight persistence: `/etc/neupod-demo.conf` (`backlight=N`), written atomically (fsync, then rename) and only if the value
changed. It is written only when leaving the Backlight screen or on SIGTERM/SIGINT; Vol+/- changes made on other screens
persist only on SIGTERM/SIGINT, so a power cut loses them.

Input details: devices are found by name under `/sys/class/input/event*/device/name` (`mtk-tpd`, `mtk-kpd`). The wheel
sends KEY_DOWN (clockwise) / KEY_UP (anticlockwise) press+release per detent; one tick is emitted per press. A wheel tick
within 150 ms after a ring-button press is dropped (a finger brushing the wheel fires a stray KEY_DOWN).

## Framebuffer findings (measured on the device)

- `/dev/fb0` (mtkfb): visible 480x360, virtual 480x1104 (three 360-line pages), 32 bpp, line_length 1920, smem_len 2119680,
  red 16/8 green 8/8 blue 0/8 transp 24/8. The code uses the reported offsets; the alpha byte is written as 0xFF.
- 32-bit ARM ioctl structs: `fb_var_screeninfo` is 160 bytes; `fb_fix_screeninfo` is 68 bytes (4-byte `unsigned long`),
  `line_length` at offset 44. Both are covered by layout tests (the fix struct test also accepts the 64-bit host layout).
- Present modes (`--fb-mode writeonly|pan|panput`, default `panput`), full 480x360 blit + present, alternating two solid
  colours (`NeuPod.Y2Demo --fb-bench`):

  | mode | fps | notes |
  | --- | --- | --- |
  | writeonly (no ioctl, page 0) | 132 | this is just the managed blit cost (~7.5 ms/frame) |
  | pan (FBIOPAN_DISPLAY, round-robin over 3 pages) | 49.2 | driver accepts it; yoffset reads back as 720 after 60 frames |
  | panput (PAN then FBIOPUT_VSCREENINFO, 3 pages) | 46.1 | what fbtool does; default |

- The pan offset persists across process exits (we found yoffset=360 left over after a run), so write-only mode first pans
  back to page 0. Reading `/dev/fb0` back shows what the app wrote, so readback proves our writes and that the driver
  accepted the pan (`fbtool info` reports `off=0,360`), but it cannot show whether the panel itself refreshed on a bare
  write without a pan. That needs eyes on the screen: run `--fb-bench` with the app stopped and watch for red/blue flashing
  in each mode. Until that is checked, the app uses `panput`, the path fbtool already proved on the panel.
- Double buffering: the app renders into a managed 480x360 buffer, blits into the hidden page, then pans to it
  (round-robin over all three pages: flips may complete asynchronously and there is no vsync wait). If the driver ever refuses a pan the framebuffer degrades to write-only instead of crashing.

## Measured on the device

- Idle CPU (main menu, nothing happening): about 0.1% (2-3 clock ticks of 100 Hz in 30 s), 23 MB RSS, 14 threads.
  The first ~30 s after start costs more while the JIT tiers up. The loop sleeps until input or the next timer (clock
  minute, 30 s background info refresh; 1 Hz on System).


## Font

Spleen 8x16 and 12x24 BDF by Frederic Cambus. These two files are BSD-2-Clause, which is different from the rest of the
repo's licence; keep the licence text with them (in
`src/NeuPod.Y2Demo/Fonts/LICENSE-spleen.txt`). They are embedded in the assembly and parsed at start-up.
