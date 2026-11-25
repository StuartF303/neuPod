#!/usr/bin/env python3
"""Minimal fastboot client for the Y2's MediaTek LK (0bb4:0c01).

LK on this unit answers `getvar max-download-size` with an empty string, which makes the
modern `fastboot` client stall. This speaks the raw protocol with a fixed command allowlist:

  lkboot.py getvar NAME
  lkboot.py boot IMAGE        # RAM boot: download + boot. UNPROVEN on this LK (see below).
  lkboot.py reboot
  lkboot.py continue
  lkboot.py reboot-recovery   # `oem reboot-recovery`: LK says OKAY, then boots RECOVERY

On this unit `boot` after `download` did NOT boot: LK stayed in fastboot and never replied.
There are deliberately NO flash/erase/arbitrary-oem subcommands: every flash write goes
through y2flash.py and its guards. Anything else is rejected before USB is opened.
Exit codes: 0 ok, 1 device refused (FAIL/unexpected), 2 usage/not found, 3 timeout/USB error.
"""
import sys

VID, PID = 0x0BB4, 0x0C01
CHUNK = 0x40000
CMD_TIMEOUT = 5000     # ms
DATA_TIMEOUT = 20000   # ms, per download chunk and final status
MAX_IMAGE = 64 * 1024 * 1024
MAX_INFO = 200         # INFO lines tolerated per command (a stuck device must not loop forever)

# Fixed command strings; the only parameterised ones are getvar NAME and download size.
SIMPLE = {"reboot": "reboot", "continue": "continue", "reboot-recovery": "oem reboot-recovery"}
SUBCOMMANDS = ("getvar", "boot") + tuple(SIMPLE)


class LkError(Exception):
    """Device-side or transport failure; message is shown to the user."""
    def __init__(self, msg, code=1):
        super().__init__(msg)
        self.code = code


def _is_timeout(exc):
    return getattr(exc, "errno", None) in (110, 60) or "timed out" in str(exc).lower()


ENODEV, EIO = 19, 5
def read_reply(ep_in, timeout=CMD_TIMEOUT):
    """One raw reply, decoded."""
    return bytes(ep_in.read(512, timeout)).decode(errors="replace")


def cmd(ep_out, ep_in, text, timeout=CMD_TIMEOUT, info=None):
    """Send a command; collect INFO lines into `info`; return the final reply (OKAY/FAIL/DATA...)."""
    ep_out.write(text.encode(), timeout)
    for _ in range(MAX_INFO):
        r = read_reply(ep_in, timeout)
        if r.startswith("INFO"):
            if info is not None:
                info.append(r[4:])
            continue
        return r
    raise LkError("too many INFO lines without a final reply")


def getvar(ep_out, ep_in, name):
    """Returns (ok, value, info_lines). FAIL or anything but OKAY gives ok=False."""
    info = []
    r = cmd(ep_out, ep_in, f"getvar:{name}", info=info)
    if r.startswith("OKAY"):
        return True, r[4:], info
    return False, r[4:] if r.startswith("FAIL") else r, info


def download(ep_out, ep_in, data):
    r = cmd(ep_out, ep_in, f"download:{len(data):08x}", DATA_TIMEOUT)
    if not r.startswith("DATA"):
        raise LkError(f"download refused: {r!r}")
    announced = int(r[4:12] or "0", 16) if r[4:12].strip("0123456789abcdefABCDEF") == "" else len(data)
    if announced not in (0, len(data)):
        raise LkError(f"device accepted {announced} bytes, not {len(data)}")
    for i in range(0, len(data), CHUNK):
        ep_out.write(data[i:i + CHUNK], DATA_TIMEOUT)
    r = read_reply(ep_in, DATA_TIMEOUT)
    if not r.startswith("OKAY"):
        raise LkError(f"download of {len(data)} bytes failed: {r!r}")


def open_dev():
    try:
        import usb.core
        import usb.util
    except ImportError:
        raise LkError("pyusb is not installed: use the venv python (tools/setup-host.sh)", 2)
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        raise LkError("LK fastboot (0bb4:0c01) not found", 2)
    try:
        cfg = dev.get_active_configuration()
        intf = usb.util.find_descriptor(cfg, bInterfaceClass=0xFF, bInterfaceSubClass=0x42)
        if intf is None:
            raise LkError("no fastboot interface on 0bb4:0c01", 2)
        if dev.is_kernel_driver_active(intf.bInterfaceNumber):
            dev.detach_kernel_driver(intf.bInterfaceNumber)
        direction = lambda want: usb.util.find_descriptor(  # noqa: E731
            intf, custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == want)
        return direction(usb.util.ENDPOINT_OUT), direction(usb.util.ENDPOINT_IN)
    except usb.core.USBError as e:
        raise LkError(f"cannot open the device: {e}", 3)


def run(argv, ep_out, ep_in, out=print):
    """Execute a validated command line against the endpoints; returns the exit code."""
    name = argv[0]
    if name == "getvar":
        ok, val, info = getvar(ep_out, ep_in, argv[1])
        for line in info:
            out(f"(bootloader) {line}")
        out(val if ok else f"FAIL: {val}")
        return 0 if ok else 1
    if name in SIMPLE:
        info = []
        r = cmd(ep_out, ep_in, SIMPLE[name], info=info)
        for line in info:
            out(f"(bootloader) {line}")
        out(f"{name}: {r}")
        return 0 if r.startswith("OKAY") else 1
    # boot
    out("warning: RAM boot is UNPROVEN on this LK: it stayed in fastboot without replying last time.")
    with open(argv[1], "rb") as f:
        data = f.read(MAX_IMAGE + 1)
    if not data or len(data) > MAX_IMAGE:
        raise LkError(f"{argv[1]}: empty or larger than {MAX_IMAGE} bytes", 2)
    download(ep_out, ep_in, data)
    out(f"downloaded {len(data)} bytes")
    r = cmd(ep_out, ep_in, "boot", 10000)
    out(f"boot: {r}")
    return 0 if r.startswith("OKAY") else 1


def parse(argv):
    """Validate arguments; returns the argv tail to run, or None if it is not allowed."""
    if not argv or argv[0] not in SUBCOMMANDS:
        return None
    want = 1 if argv[0] in ("getvar", "boot") else 0
    if len(argv) != 1 + want:
        return None
    if argv[0] == "getvar" and not (argv[1] and all(c.isascii() and (c.isalnum() or c in "-_.") for c in argv[1])):
        return None   # no ':' or whitespace: NAME can't smuggle in a second command
    return argv


def main(argv, endpoints=None):
    args = parse(argv[1:])
    if args is None:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        ep_out, ep_in = endpoints or open_dev()
        return run(args, ep_out, ep_in)
    except LkError as e:
        print(f"error: {e}", file=sys.stderr)
        return e.code
    except OSError as e:
        # pyusb's USBError is an OSError subclass; tell it apart from plain file errors.
        if e.__class__.__name__ not in ("USBError", "USBTimeoutError") and not _is_timeout(e):
            print(f"error: {e}", file=sys.stderr)
            return 2
        if _is_timeout(e):
            print(f"error: timed out talking to LK ({e})", file=sys.stderr)
            return 3
        # Only "device gone" counts as a normal end: ENODEV always, EIO only for commands that
        # reboot LK. Any other USB error is a real failure.
        errno_ = getattr(e, "errno", None)
        if args[0] != "getvar" and (errno_ == ENODEV or (errno_ == EIO and args[0] != "boot")):
            print(f"{args[0]}: device left the bus ({e}); normal after this command")
            return 0
        print(f"error: USB error: {e}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv))
