#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Drive Android's system folder picker (DocumentsUI) with adb, for the
folder-access integration test: open Music, then FOLDER, then "Use this
folder" and "Allow". Exits 0 once access is allowed, 1 on timeout.

Usage: drive_saf_picker.py [-s SERIAL] [--folder neupod_it] [--timeout 180]
"""

import argparse
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET


def adb(serial, *args, check=True):
    cmd = ["adb"] + (["-s", serial] if serial else []) + list(args)
    return subprocess.run(cmd, capture_output=True, text=True, check=check).stdout


def focus(serial):
    out = adb(serial, "shell", "dumpsys", "window", check=False)
    m = re.search(r"mCurrentFocus=.*", out)
    return m.group(0) if m else ""


def dump(serial):
    adb(serial, "shell", "uiautomator", "dump", "/sdcard/neupod_ui.xml", check=False)
    xml = adb(serial, "shell", "cat", "/sdcard/neupod_ui.xml", check=False)
    try:
        return ET.fromstring(xml[xml.find("<"):])
    except ET.ParseError:
        return None


def nodes(root):
    for n in root.iter("node"):
        yield n


def centre(node):
    a, b, c, d = map(int, re.findall(r"\d+", node.get("bounds", "[0,0][0,0]")))
    return (a + c) // 2, (b + d) // 2


def find(root, pred):
    for n in nodes(root):
        if pred(n):
            return n
    return None


def text_is(*words):
    wanted = {w.lower() for w in words}
    return lambda n: (n.get("text") or "").strip().lower() in wanted


def tap(serial, node, label):
    x, y = centre(node)
    print(f"drive_saf_picker: tap {label} at {x},{y}", flush=True)
    adb(serial, "shell", "input", "tap", str(x), str(y))
    time.sleep(1.5)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-s", "--serial")
    ap.add_argument("--folder", default="neupod_it")
    ap.add_argument("--timeout", type=int, default=180)
    args = ap.parse_args()
    s = args.serial
    deadline = time.time() + args.timeout
    in_music = False
    in_folder = False
    while time.time() < deadline:
        f = focus(s)
        if "documentsui" not in f and "permissioncontroller" not in f:
            if in_folder:
                print("drive_saf_picker: picker closed", flush=True)
                return 0
            time.sleep(1)
            continue
        root = dump(s)
        if root is None:
            time.sleep(1)
            continue
        # Confirmation dialog: "Allow <app> to access files in <folder>?"
        allow = find(root, text_is("allow"))
        if allow is not None and in_folder:
            tap(s, allow, "Allow")
            continue
        if in_folder:
            use = find(root, text_is("use this folder"))
            if use is not None:
                tap(s, use, "Use this folder")
                continue
        target = find(root, text_is(args.folder))
        if target is not None and in_music:
            tap(s, target, args.folder)
            in_folder = True
            continue
        music = find(root, text_is("music"))
        if music is not None and not in_music:
            tap(s, music, "Music")
            in_music = True
            continue
        # Not at the storage root yet: open the roots drawer and choose the
        # device's internal storage (its label varies by device).
        if not in_music:
            drawer = find(root, lambda n: (n.get("content-desc") or "").lower() in ("show roots", "open navigation drawer"))
            if drawer is not None:
                tap(s, drawer, "roots drawer")
                root = dump(s)
                storage = root is not None and find(
                    root,
                    lambda n: n.get("resource-id", "").endswith(":id/title")
                    and re.search(r"(sdk_gphone|internal|pixel|phone|storage)", (n.get("text") or ""), re.I),
                )
                if storage:
                    tap(s, storage, storage.get("text"))
                continue
        time.sleep(1)
    print("drive_saf_picker: timed out", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
