#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""A fake card whose names need characters beyond ASCII, for the text tour (docs/ui.md "Fonts and display text").

usage: make_text_card.py OUTDIR AUDIO_ASSETS_DIR

Writes, at the card root: a folder with an umlaut and U+00BF holding the only playable FLAC (the real 0.3 s test vector with accented tags: a title with
U+00BF, an artist with accents, an album with curly quotes and an en dash) named like the owner's ripper does ("02 Have You Fed the Fish<U+00BF>.flac"),
and files whose names hold Latin-1 letters, Latin Extended-A, curly quotes, CJK and an emoji (listed, not playable).
"""
import importlib.util
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("make_library_card", os.path.join(HERE, "make_library_card.py"))
lib = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lib)


def main():
    out, assets = sys.argv[1], sys.argv[2]
    shutil.rmtree(out, ignore_errors=True)  # every run starts from default settings
    tags = [("TITLE", "Have You Fed the Fish¿ (Extended Remastered Radio Mix)"), ("ARTIST", "Björk & Sigur Rós — Ñandú"),
            ("ALBUM", "“Ágætis byrjun” – Stuart’s mix"), ("TRACKNUMBER", "2")]
    lib.put(os.path.join(out, "Motörhead ¿Qué?", "02 Have You Fed the Fish¿.flac"),
            lib.flac_with_tags(os.path.join(assets, "flac16_44k_st.flac"), tags))
    for name in ("Café Tacvba – Ñ.txt", "Ørjan Åsmund ß ü.txt", "“Hoppípolla” — Sigur Rós.txt",
                 "Łódź œuvre Š.txt", "日本語のタイトル.txt", "Party \U0001F389 mix.txt",
                 "A file name far too long to fit on one line of the list.txt"):
        lib.put(os.path.join(out, name), b"x")


main()
