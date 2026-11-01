# ADR-0003: Audio quality policy

## Status

Proposed

## Context

neuPod plays local files in MP3, AAC, FLAC, ALAC and Opus to wired outputs and to Bluetooth
A2DP sinks. Each processing stage is an opportunity to lose quality.

## Decision

- Lossless end-to-end on wired/digital paths.
- Decode once, into a 32-bit mixer.
- No resampling unless the sink requires it.
- Codecs: MP3, AAC, FLAC, ALAC, Opus.
- Bluetooth A2DP via BTstack (SBC / SBC-XQ) on the CYW43439; an external Qualcomm QCC module
  is the alternative.

## Consequences

- `hal_audio.h` takes signed 32-bit interleaved samples at the source rate and reports which
  rates the sink can run natively.
- Bluetooth output is lossy by nature (SBC); the policy applies to wired/digital paths.
- Memory and CPU budgets must allow a 32-bit mix buffer.
