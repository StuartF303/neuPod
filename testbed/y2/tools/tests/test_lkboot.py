"""lkboot.py against a fake USB endpoint pair (no device)."""
import importlib.util
import io
import os
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

_spec = importlib.util.spec_from_file_location("lkboot", Path(__file__).resolve().parents[1] / "lkboot.py")
lk = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(lk)


class FakeTimeout(OSError):
    def __init__(self):
        super().__init__(110, "Operation timed out")


class Fake:
    """Both endpoints in one object: records writes, replays queued replies."""
    def __init__(self, replies=()):
        self.writes, self.replies = [], list(replies)

    def write(self, data, timeout=None):
        self.writes.append(bytes(data))

    def read(self, size, timeout=None):
        if not self.replies:
            raise FakeTimeout()
        r = self.replies.pop(0)
        if isinstance(r, Exception):
            raise r
        return r if isinstance(r, bytes) else r.encode()


def run(argv, fake):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        rc = lk.main(["lkboot.py"] + argv, endpoints=(fake, fake))
    return rc, out.getvalue(), err.getvalue()


class Getvar(unittest.TestCase):
    def test_okay_with_info_lines(self):
        f = Fake(["INFOpart one", "INFOpart two", "OKAYvalue"])
        rc, out, _ = run(["getvar", "version"], f)
        self.assertEqual((rc, f.writes), (0, [b"getvar:version"]))
        self.assertIn("(bootloader) part one", out)
        self.assertIn("(bootloader) part two", out)
        self.assertIn("value", out)

    def test_empty_okay_is_still_ok(self):
        self.assertEqual(run(["getvar", "max-download-size"], Fake(["OKAY"]))[0], 0)

    def test_fail(self):
        rc, out, _ = run(["getvar", "nope"], Fake(["FAILunknown variable"]))
        self.assertEqual(rc, 1)
        self.assertIn("FAIL: unknown variable", out)

    def test_timeout_is_clean(self):
        rc, _, err = run(["getvar", "x"], Fake([]))
        self.assertEqual(rc, 3)
        self.assertIn("timed out", err)
        self.assertNotIn("Traceback", err)

    def test_name_cannot_smuggle_a_command(self):
        for bad in ("a:b", "a b", "", "a;b", "caf\u00e9", "\u0663"):
            f = Fake(["OKAY"])
            self.assertEqual(run(["getvar", bad], f)[0], 2, bad)
            self.assertEqual(f.writes, [])


class Simple(unittest.TestCase):
    def test_reboot_recovery_command_string(self):
        f = Fake(["OKAY"])
        self.assertEqual(run(["reboot-recovery"], f)[0], 0)
        self.assertEqual(f.writes, [b"oem reboot-recovery"])

    def test_reboot_and_continue(self):
        for name in ("reboot", "continue"):
            f = Fake(["OKAY"])
            self.assertEqual(run([name], f)[0], 0)
            self.assertEqual(f.writes, [name.encode()])

    def test_fail_reply(self):
        self.assertEqual(run(["reboot-recovery"], Fake(["FAILnot allowed"]))[0], 1)

    def test_device_gone_after_reboot_is_ok(self):
        class USBError(OSError):
            pass
        rc, out, _ = run(["reboot"], Fake([USBError(19, "No such device")]))
        self.assertEqual(rc, 0)
        self.assertIn("left the bus", out)


class UsbErrors(unittest.TestCase):
    @staticmethod
    def usb_error(errno):
        class USBError(OSError):
            pass
        return USBError(errno, "usb")

    def test_enodev_ok_for_reboot_commands(self):
        for name in ("reboot", "continue", "reboot-recovery"):
            self.assertEqual(run([name], Fake([self.usb_error(19)]))[0], 0, name)

    def test_eio_ok_only_for_reboot_commands(self):
        for name in ("reboot", "continue", "reboot-recovery"):
            self.assertEqual(run([name], Fake([self.usb_error(5)]))[0], 0, name)
        rc, _, err = run(["getvar", "x"], Fake([self.usb_error(5)]))
        self.assertEqual(rc, 3)
        self.assertIn("USB error", err)

    def test_other_errors_are_failures(self):
        for name in ("reboot", "continue", "reboot-recovery"):
            for errno in (32, 71, 16):  # EPIPE, EPROTO, EBUSY
                rc, _, err = run([name], Fake([self.usb_error(errno)]))
                self.assertEqual(rc, 3, (name, errno))
                self.assertIn("USB error", err)

    def test_missing_pyusb_is_clean(self):
        import sys
        saved = {k: sys.modules.get(k) for k in ("usb", "usb.core", "usb.util")}
        sys.modules.update({"usb": None, "usb.core": None, "usb.util": None})  # None makes import raise
        try:
            out, err = io.StringIO(), io.StringIO()
            with redirect_stdout(out), redirect_stderr(err):
                rc = lk.main(["lkboot.py", "reboot"])
        finally:
            for k, v in saved.items():
                if v is None:
                    sys.modules.pop(k, None)
                else:
                    sys.modules[k] = v
        self.assertEqual(rc, 2)
        self.assertIn("pyusb", err.getvalue())
        self.assertNotIn("Traceback", err.getvalue())


class Refusal(unittest.TestCase):
    def test_other_commands_rejected_without_touching_usb(self):
        for argv in (["flash", "boot", "x.img"], ["erase", "boot"], ["oem", "format"], ["oem", "reboot-recovery"],
                     ["download", "x"], ["getvar"], ["getvar", "a", "b"], ["reboot", "now"], ["boot"], [],
                     ["reboot; flash"], ["flash:boot"]):
            f = Fake(["OKAY"] * 3)
            rc, _, err = run(argv, f)
            self.assertEqual(rc, 2, argv)
            self.assertEqual(f.writes, [], argv)
            self.assertIn("NO flash", err)

    def test_source_has_no_flash_or_erase_commands(self):
        src = (Path(lk.__file__)).read_text()
        for bad in ('"flash', "'flash", '"erase', "f\"flash", "oem format", "oem unlock"):
            self.assertNotIn(bad, src)


class Boot(unittest.TestCase):
    def image(self, n):
        fd, p = tempfile.mkstemp()
        os.write(fd, bytes(i % 251 for i in range(n)))
        os.close(fd)
        self.addCleanup(os.unlink, p)
        return p

    def test_download_chunking_and_boot(self):
        n = lk.CHUNK * 2 + 123
        f = Fake([f"DATA{n:08x}", "OKAY", "OKAY"])
        rc, out, _ = run(["boot", self.image(n)], f)
        self.assertEqual(rc, 0)
        self.assertIn("UNPROVEN", out)
        self.assertEqual(f.writes[0], f"download:{n:08x}".encode())
        chunks = f.writes[1:-1]
        self.assertEqual([len(c) for c in chunks], [lk.CHUNK, lk.CHUNK, 123])
        self.assertEqual(b"".join(chunks), bytes(i % 251 for i in range(n)))
        self.assertEqual(f.writes[-1], b"boot")

    def test_download_refused(self):
        rc, _, err = run(["boot", self.image(100)], Fake(["FAILtoo large"]))
        self.assertEqual(rc, 1)
        self.assertIn("download refused", err)

    def test_no_reply_to_boot_is_timeout_not_traceback(self):
        f = Fake([f"DATA{100:08x}", "OKAY"])
        rc, _, err = run(["boot", self.image(100)], f)
        self.assertEqual(rc, 3)
        self.assertNotIn("Traceback", err)

    def test_missing_image(self):
        rc, _, err = run(["boot", "/nonexistent/x.img"], Fake([]))
        self.assertEqual(rc, 2)


if __name__ == "__main__":
    unittest.main()
