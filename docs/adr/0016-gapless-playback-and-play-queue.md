# ADR-0016: Gapless playback and the play queue

Status: accepted (bench verification pending)

## Context

Tracks must follow each other by themselves, without a gap, for albums that were recorded to run on (live albums, DJ mixes). Until
now one file played, core 1 drained and ramped down, and the next start flushed everything. There is no library database yet, so
"what comes next" has to come from the folder the track was chosen in. ADR-0003 forbids resampling; ADR-0011 limits what core 1 may do.

## Decision

* **Queue** (core 0, `play_queue.c`): playing a file from Files queues the playable files of its folder in Files order, from the
  chosen one on (earlier ones stay for Previous). Non-audio entries are skipped. Next and Previous are hard starts (ramp, and a
  re-clock if the rate differs); Previous after more than 3 s restarts the track. The end of the queue stops playback and Now Playing
  goes idle. Static cost: 3.6 KB (names packed in a 3 KB pool, 128 tracks at most; a bigger folder keeps a window around the chosen track).
* **Handshake** (details in `docs/audio.md`): streams get sequence numbers and live in two slots (`seq & 1`) holding the description
  and the stream's length in bytes. Core 0 announces the successor with `AUDIO_CMD_STREAM_NEXT` once the current decoder is open,
  and publishes each stream's length when its last byte is in the data ring; the successor's bytes may follow at once. Core 1 reads
  the ring before the length, so seeing the next stream's bytes implies seeing the boundary. A cancelled announcement is a cleared
  `g_audio_next_valid`, checked again when core 1 takes it over (copy, re-check). Everything is single-writer, no locks.
* **Core 1 switch**: when the decoder has delivered its last frame and a compatible successor is announced (same rate and channel
  count), the decoder is replaced in place: sink, PCM ring and volume are untouched. The PCM ring position of the boundary goes to the
  block interrupt through a small SPSC ring; it reports `END_OF_TRACK` (value 1) when the block holding the last sample is served, so
  the UI changes tracks with the sound, to within one 256-frame block. Different rate or channels: core 0 announces nothing, the track
  ends the ordinary way (drain, ramp, re-clock) and the player starts the next one.
* **Encoder delay and padding**: FLAC and WAV are exact. MP3: `dr_mp3` already trims the LAME delay (+529) and padding (-529); the parser
  reports the same length (tested against the decoder, never trimmed twice). ALAC: the edit list (one non-empty entry) else `iTunSMPB`;
  the codec drops the priming and stops at the exact length, the encoder padding left in the stream is skipped to the stream end.
* **Setting** "Gapless playback" (settings store, default on). Off: tracks still advance, with the ordinary start in between.

## Consequences

* Static RAM +6.4 KB measured on the final build against origin/main (`arm-none-eabi-objdump -h`: .bss 0x75c18 to 0x77518 = +6,400 B,
  .data +152 B): queue 3.6 KB, second track context with both paths 1.7 KB, slots 0.3 KB, boundary ring and bookkeeping the rest.
  No extra codec memory: the arena is reused by the successor's decoder after the old one has finished.
* The successor's header is parsed from the card while the current track plays (one 2 KB-buffered pass, with the data ring full).
* Only one handoff can be pending: tracks shorter than about 0.3 s cannot chain gaplessly (they still play, with the ordinary start).
* A decoder that needs more than the whole file before it opens (a file smaller than the 24 KB read chunk of the MP3 decoder) cannot
  be a gapless successor of another track earlier than its end is published: such a file simply falls back to the ordinary start.
* Core-1 additions are small (switch, boundary list); core-1 closure checks pass.

## Known limits

* If the block interrupt stalls for more than 100 ms (FLUSH_TIMEOUT_US) during a flush, the loop proceeds and boundaries still queued
  may be dropped: a gapless track change would then not be reported (the next hard start re-synchronises). Not seen on the bench.
* A folder with more than 128 playable files, or more than 3 KB of names, queues a window around the chosen track; the album then ends
  at the window's end without saying so. A later UI hint ("queue limited") would be the remedy.
* The art service restarts its decode when a gapless handoff changes the album mid-decode (a job generation is re-checked after every
  yield); a same-album handoff costs nothing.
* The player logs one line per track change on the UART (`player: track 3/6 'name' (start|next|prev|restart|gapless|auto|skip|repeat|wrap)`, `player: queue end`; a handoff made by the audio core reads `gapless`, `gapless repeat` or `gapless wrap`).

## Amendment: shuffle and repeat

* Shuffle (off / songs) and repeat (off / one / all) are settings (`NPsh`, `NPrp`, default off), applied to the queue being played at once.
* The queue keeps entries in folder order plus a play-order array (`order[slot]`, 256 B in `play_queue_t`, no allocation); `pos` is a slot.
  Shuffle is a seeded Fisher-Yates permutation (xorshift32, seeded from the tick count at the user's action) with the current track at slot 0,
  so turning it on mid-play keeps the track; turning it off returns to folder order at the same track. Previous walks back through the
  shuffled order; more than 3 s in it restarts as before.
* Repeat-one only applies when a track ends by itself (and to the gapless look-ahead, which then offers the same file again); the Next
  button still moves on. Repeat-all wraps to slot 0 instead of `queue end`; with shuffle it re-shuffles and never opens the new round with
  the track that just ended (unless the queue has one track). The opening track is fixed when the look-ahead asks (`wrap_first`), so the
  announced handoff and the later move agree.
* The gapless descriptor handoff is unchanged. If a mode changes after the next track was announced to the audio core, the player records
  the announced queue entry and, when the handoff happens, pulls exactly that entry in as the next track (`pq_commit`) rather than trusting
  the new order. A failed start request after a move puts the queue back exactly as it was (a file-static 264 B snapshot of the order, taken only when shuffle is on; kept off the task stacks).
* Turning shuffle on mid-play puts the current track at slot 0, so Previous restarts it (there is no history before it).
* Under repeat-all an unplayable track at the end is skipped by wrapping, at most one lap in a row, so an all-broken queue ends in the error.
* Cost: `.bss` +268 B (one queue instance) + 264 B (snapshot), flash about +3.3 KB. The main menu's "Shuffle Songs" stays a placeholder (needs the library).

