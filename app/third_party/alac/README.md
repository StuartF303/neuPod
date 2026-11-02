# Apple Lossless (ALAC) decoder core (vendored)

From https://github.com/macosforge/alac, pinned commit `c38887c5c5e64a4b31108733bd79ca9b2496d987`.

Licence: Apache License 2.0, Copyright (c) 2011 Apple Inc. (`LICENSE`, `APPLE_LICENSE.txt`). The repository has no NOTICE
file. Every vendored file keeps Apple's licence header.

Unmodified from upstream: `ag_dec.c`, `dp_dec.c`, `matrix_dec.c`, `ALACBitUtilities.c/.h`, `EndianPortable.c/.h`,
`aglib.h`, `dplib.h`, `matrixlib.h`, `ALACAudioTypes.h`.

Added by neuPod (Apache-2.0 section 4(b), modifications): `alac_dec.c/.h`, a C port of `ALACDecoder.cpp` (the class becomes a
struct; a partial-frame sample count above `frameLength` is rejected, because the original trusted the file and could overrun its buffers; the three work buffers come from a caller-supplied allocator, so the decoder runs on the bare-metal audio core
without malloc; the magic cookie parser is reduced to what is needed). The MP4 container is handled by neupod's own
demuxer (`app/audio/mp4.c`).

The reference encoder (`ALACEncoder.cpp` etc.) is **not** part of the firmware; it lives in `tests/tools/alac_encoder/`
and is built only on the host by `tools/gen_alac_vectors.py` to produce test files.
