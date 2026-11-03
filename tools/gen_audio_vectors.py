#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Generates the codec test vectors in tests/assets/audio/ (committed; regenerate only if you change them):
    uv run --no-project --with soundfile --with numpy python tools/gen_audio_vectors.py tests/assets/audio
For each vector it writes the encoded file and <name>.md5 = MD5 of the SOURCE samples as FLAC defines it
(interleaved, ceil(bits/8) little-endian bytes per sample), computed here independently of the C code."""
import hashlib, sys, os
import numpy as np
import soundfile as sf

out = sys.argv[1]
rng = np.random.default_rng(12345)

def tone(rate, secs, ch, bits):
    n = int(rate * secs)
    t = np.arange(n) / rate
    sig = 0.35 * np.sin(2 * np.pi * 440 * t) + 0.2 * np.sin(2 * np.pi * 2217 * t) + 0.02 * rng.standard_normal(n)
    sig = np.clip(sig, -0.999, 0.999)
    if ch == 2:
        sig = np.stack([sig, 0.8 * np.roll(sig, 7) + 0.05 * np.sin(2 * np.pi * 97 * t)], axis=1)
    full = 2 ** (bits - 1)
    return np.round(sig * (full - 1)).astype(np.int64)

def md5_of(x, bits):
    b = (bits + 7) // 8
    flat = x.reshape(-1)
    raw = b"".join(int(v).to_bytes(b, "little", signed=True) for v in flat)
    return hashlib.md5(raw).hexdigest()

vectors = [("flac16_44k_st", 44100, 0.3, 2, 16), ("flac24_96k_st", 96000, 0.2, 2, 24),
           ("flac16_48k_mono", 48000, 0.3, 1, 16), ("flac24_48k_st", 48000, 0.25, 2, 24)]
for name, rate, secs, ch, bits in vectors:
    x = tone(rate, secs, ch, bits)
    # libsndfile reads int32 as left-justified 32-bit full scale, hence the shift
    sf.write(os.path.join(out, name + ".flac"), (x << (32 - bits)).astype(np.int32), rate, format="FLAC", subtype="PCM_16" if bits == 16 else "PCM_24")
    open(os.path.join(out, name + ".md5"), "w").write(md5_of(x, bits) + " %d %d %d %d\n" % (rate, ch, bits, len(x)))

# MP3: lossy, so the check is length and level (RMS per channel of the SOURCE is recorded)
rate = 44100
n = rate
t = np.arange(n) / rate
l = 0.4 * np.sin(2 * np.pi * 440 * t) + 0.1 * np.sin(2 * np.pi * 3000 * t)
r = 0.25 * np.sin(2 * np.pi * 660 * t)
st = np.stack([l, r], axis=1)
sf.write(os.path.join(out, "mp3_44k_st.mp3"), st, rate, format="MP3")
rms = lambda v: float(np.sqrt(np.mean(v ** 2)))
open(os.path.join(out, "mp3_44k_st.txt"), "w").write("%d 2 %d %.6f %.6f\n" % (rate, n, rms(l), rms(r)))
