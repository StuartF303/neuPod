# Apple Lossless (ALAC) decoder core (vendored)

From https://github.com/macosforge/alac, pinned commit `c38887c5c5e64a4b31108733bd79ca9b2496d987`.

Licence: Apache License 2.0, Copyright (c) 2011 Apple Inc. (`LICENSE`, `APPLE_LICENSE.txt`). The repository has no NOTICE
file. Every vendored file keeps Apple's licence header.

Unmodified from upstream: `ALACBitUtilities.h`, `EndianPortable.h`, `aglib.h`, `dplib.h`, `matrixlib.h`, `ALACAudioTypes.h`.

Modified by neuPod: `EndianPortable.c` detects the byte order from the compiler's `__BYTE_ORDER__` (upstream knew only x86 and Win32, so on ARM the big-endian cookie fields were not swapped) and fails the build if it is unknown. `alac_dec.c` copies the cookie config with `memcpy` instead of casting a possibly unaligned pointer.

Modified by neuPod (RAM placement, no functional change): `ag_dec.c` (`dyn_decomp`), `dp_dec.c` (`unpc_block`), `matrix_dec.c`
(`unmix16/20/24`, `copyPredictorTo20/24/24Shift`), `ALACBitUtilities.c` (`BitBufferRead/ReadSmall/ReadOne/Advance`)
and `alac_dec.c` (`alac_dec_decode`, which contains the inlined `Decode`) wrap the function name in `NEUPOD_RAM_FUNC()`
(`app/include/ram_func.h`) and include that header. On the RP2350 this places them in SRAM so the audio core does not
stall on XIP flash cache misses; on other targets the macro is empty. Not moved: the 32-bit paths `unmix32`, `copyPredictorTo32/32Shift`
(32-bit ALAC is rejected before decoding), init/setup helpers, `BitBufferInit/ByteAlign/Peek*`.

Added by neuPod (Apache-2.0 section 4(b), modifications): `alac_dec.c/.h`, a C port of `ALACDecoder.cpp` (the class becomes a
struct; a partial-frame sample count above `frameLength` is rejected, because the original trusted the file and could overrun its buffers; the three work buffers come from a caller-supplied allocator, so the decoder runs on the bare-metal audio core
without malloc; the magic cookie parser is reduced to what is needed). The MP4 container is handled by neupod's own
demuxer (`app/audio/mp4.c`).

The reference encoder (`ALACEncoder.cpp` etc.) is **not** part of the firmware; it lives in `tests/tools/alac_encoder/`
and is built only on the host by `tools/gen_alac_vectors.py` to produce test files.
