# dr_libs (vendored)

Single-header audio decoders by David Reid, https://github.com/mackron/dr_libs

* Pinned commit: `35b5c392cc15456736c440a68dbe073178bbffe0` (`master` at the time of vendoring)
* Files taken unmodified: `dr_flac.h` (v0.13.4), `dr_mp3.h` (contains minimp3, CC0), `dr_wav.h`, and the repository's `LICENSE`.
* Licence: public domain or MIT No Attribution (each header ends with both texts).
* Used: `dr_flac.h` and `dr_mp3.h`, compiled once in `app/audio/codec_dr.c` and configured by
  `app/include/codec_dr_config.h` (no stdio, no malloc: all memory comes from the audio core's arena; no Ogg, no SIMD, MP3 only,
  no CRC checks, asserts disabled).
* `dr_wav.h` is vendored (as asked) but **not compiled**: neuPod's WAV path is raw PCM passthrough with its own ~100-line header
  parser (`app/audio/wav.c`), which has no heap use, walks the chunks through a callback, and is unit-tested.
  dr_wav adds container features (ADPCM, A-law, float conversion, writing) that the player does not use.
* Updating: copy the three headers, re-run `ctest` (the codec tests are bit-exact) and the RP2350 build (its post-build check
  verifies the core-1 call graph).
