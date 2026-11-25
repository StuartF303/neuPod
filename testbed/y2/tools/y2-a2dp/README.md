# y2-a2dp

Bring-up tool for the Innioasis Y2 testbed: streams audio to a Bluetooth headset (A2DP source, AVRCP target and controller)
with BTstack running in userspace over the kernel's `/dev/stpbt` character device. The Y2 kernel has no Bluetooth stack
(no BlueZ, no `CONFIG_BT`), so this is the only Bluetooth path on the device. Testbed tool, not product code.

## Pieces

| File | What it does |
| --- | --- |
| `platforms/y2/hal/btstack_uart_stpbt.c` (shared with the firmware) | `btstack_uart_t` for a device that is not a tty and returns one whole H4 packet per `read()`. It buffers the packet and serves the byte counts the stock `hci_transport_h4` asks for. Baud rate, parity and flow control are no-ops. |
| `platforms/y2/hal/btstack_chipset_mtk_consys.c` (shared) | BTstack chipset driver: the MediaTek vendor commands (Set_Local_BD_Addr, Set_Radio, Set_TX_Power_Offset, Set_Sleep_Timeout). They run through `hci_enable_custom_pre_init()`, so they go out before HCI_Reset. |
| `main.c` | The tool: `scan` and `play`, adapted from BTstack's `a2dp_source_demo.c`. |
| `test/` | Host tests (see below). |

The Bluetooth address comes from `/data/BT_Addr` (first 6 bytes, MSB first) at start-up. The tool never writes to
`/data/BT_Addr` or `/data/nvram`. Link keys and the last headset live in `/opt/neupod-bt/y2-a2dp.tlv` (created on first run).

## Build

BTstack is used in place from the Pico SDK (`$PICO_SDK_PATH/lib/btstack`; pico-sdk 2.3.1: BTstack 1.6.2, git eb0bb8b5e) or from `-DBTSTACK_ROOT=<dir>`.

```sh
# Y2 binary: static ARM hard-float, built with the Buildroot toolchain (see cmake/toolchain-y2.cmake for overrides)
cmake -S testbed/y2/tools/y2-a2dp -B build/y2-a2dp -DY2A2DP_TARGET=y2
cmake --build build/y2-a2dp          # build/y2-a2dp/y2-a2dp, plus y2-a2dp.stripped

# PC build: compiles everything (it cannot open the device) and runs the unit tests
cmake -S testbed/y2/tools/y2-a2dp -B build/y2-a2dp-host
cmake --build build/y2-a2dp-host && ctest --test-dir build/y2-a2dp-host --output-on-failure
```

Copy `y2-a2dp.stripped` to the device (for example to `/opt/neupod-bt/y2-a2dp`).

## Run

Prerequisite: the stock combo-chip loader and launcher must already have powered the chip in this boot, and
`/dev/stpbt` must exist. The Y2 testbed notes cover that procedure; the tool does not do it. Stop anything else that holds
`/dev/stpbt`. Opening the device takes about 1.3 s (chip power-on).

```sh
y2-a2dp scan                                   # 10 s inquiry; prints address, RSSI, class, name
y2-a2dp play                                   # find a device named like "AirPods", pair, connect, stream a 440 Hz sine for 30 s
y2-a2dp play --name "Pro" --seconds 60
y2-a2dp play --addr AA:BB:CC:DD:EE:FF --wav music.wav
y2-a2dp play --last                            # the headset streamed to last time, no inquiry
y2-a2dp play --hci-dump /tmp/bt.pklg           # HCI trace in PacketLogger format (also for scan)
```

- First pairing: put the headset in pairing mode (AirPods: case open, hold the button until the light flashes white). A bonded
  headset that is not in pairing mode does not answer an inquiry; use `--addr` or `--last` for it.
- `--wav` needs 16-bit PCM, stereo, 44.1 kHz. The stream ends at the end of the file. Without `--wav` the tool plays a 440 Hz
  sine at -12 dBFS; `--seconds` defaults to 30 for it.
- Ctrl-C (or SIGTERM) closes the stream, disconnects and powers the controller off before exiting. Every state change and
  Bluetooth event is logged to stderr with a timestamp; the scan table goes to stdout.
- Exit codes: 0 done, 3 headset not found, 4 transport error (chip reset or `/dev/stpbt` closed), 5 link or stream failure,
  64 bad usage, 130 interrupted.

## Tests

- `uart_stpbt`: one-packet-per-read framing against the stock H4 transport (split requests, back-to-back packets, a 1000-byte
  ACL, zero-length events, writes, EOF), using a `SOCK_SEQPACKET` socket pair.
- `chipset_mtk`: golden bytes of the four vendor commands.
- `hci_preinit`: real BTstack HCI layer against a fake chip; checks the wire order (vendor script, then HCI_Reset).

## Licence

The sources in this directory are MPL-2.0, but `y2-a2dp` links BTstack statically, so the binary is also covered by
BlueKitchen's BTstack licence, which permits non-commercial use only. Treat the binary as a testbed tool: do not
redistribute it or ship it in a product without a commercial BTstack licence. BTstack itself is not vendored here.
