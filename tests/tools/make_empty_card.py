#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""A fake card with a Music folder and no audio in it, for the empty-library tour (LB-4).

usage: make_empty_card.py OUTDIR
"""
import os
import shutil
import sys

out = sys.argv[1]
shutil.rmtree(out, ignore_errors=True)  # every run starts from no index and the default settings
os.makedirs(os.path.join(out, "Music"))
with open(os.path.join(out, "Music", "notes.txt"), "wb") as f:  # not audio: the scan must not count it
    f.write(b"x")
