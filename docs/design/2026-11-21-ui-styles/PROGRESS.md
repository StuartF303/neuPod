# Progress: UI styles (Classic and Modern)

The checklist for [IMPLEMENTATION-BRIEF.md](IMPLEMENTATION-BRIEF.md). Tick an item when its PR merges and add the PR
number and date: `- [x] item (#123, 2026-12-01)`. Mark a blocked item `BLOCKED: reason` and leave it unticked.

Every item means: built in each style it applies to, covered by tour snapshots at 320x240, 480x360 and 600x450, layout
audit clean, all platforms build, Pico 2 W size delta reported.

## Phase 0: measure

- [ ] Timing harness on RP2350-Touch-AMOLED-2.41
- [ ] Timing harness on ESP32-S3-Touch-AMOLED-2.41
- [ ] Backdrop generation: time and memory per track
- [ ] `PHASE0-RESULTS.md` written; **stop and show the owner**

## Phase 1: the style layer

- [ ] Checklist reconciled against `app/ui/*.c` and the tours
- [ ] Theme is style x mode; skins interface; today's look kept as the default
- [ ] Compile-time gate so the frozen Pico 2 W image is unchanged
- [ ] Classic Light colour table and contrast test
- [ ] Modern Dark colour table and contrast test
- [ ] Accent from album art, lightness clamped to 4.5:1 with white text (host-tested)
- [ ] Settings > Display > Style, stored
- [ ] Tours can run in a named style; ctest runs each tour in both
- [ ] Classic Dark. BLOCKED: not designed (README Open question 1)
- [ ] Modern Light. BLOCKED: not designed (README Open question 1)

## Phase 2: status bar

- [ ] Three-zone layout, same on every screen
- [ ] Battery skin, Classic
- [ ] Battery skin, Modern
- [ ] Play-state glyphs: stopped, play, pause
- [ ] Shuffle, repeat all, repeat one, shuffle with repeat; modes stay while paused
- [ ] Context text after the glyphs (track position, counts); none on Home and menus
- [ ] Hardware icons in fixed order, closing up when absent
- [ ] Headphones icon only while a plug is in the jack
- [ ] Jack-plug icon when the output is Line (new glyph: licence and Licences screen)
- [ ] Bluetooth: dim when saved, blue when connected or in use
- [ ] Clock removed from the status bar and the Home pane
- [ ] Title swaps to a message for about 2 s
- [ ] Title swaps to a volume bar; volume wins over a message
- [ ] 320x240 with shuffle and repeat both on. BLOCKED until Open question 12 is answered, if it does not fit

## Phase 3: feedback

- [ ] Table of every `ui_toast_show()` call and its new place, added to `docs/ui.md`
- [ ] Status-bar messages (output and link changes, transient refusals)
- [ ] Row flash with a tick, Classic (Open question 3: flash or blink)
- [ ] Row flash with a ring and a tick, Modern
- [ ] Greyed row with its reason at the right
- [ ] Centred panel, Classic
- [ ] Centred panel, Modern
- [ ] Panel messages (battery, card, settings store, library update stopped)
- [ ] `Can't open: too many screens` no longer shown to the user
- [ ] Toast strip removed
- [ ] Bottom volume HUD removed

## Phase 4: Now Playing

- [ ] Page 1 layout without pills and volume strip; small format line
- [ ] Wheel swaps the progress bar for the volume bar with speaker glyphs; reverts at rest
- [ ] Progress and volume bar skins, Classic (with the art reflection)
- [ ] Progress and volume skins, Modern (floating capsule)
- [ ] Modern backdrop from the art, made once per track; plain ground with no art
- [ ] Seek mode in both styles
- [ ] Page 2 in both styles
- [ ] Bluetooth output: codec in the format line, headset volume on the same bar

## Phase 5: lists and menus

- [ ] Selection bar skin, Classic (full width, gradient)
- [ ] Selection bar skin, Modern (inset, rounded, accent)
- [ ] Scroll bar skins
- [ ] List ground and dividers, Modern
- [ ] Home, playing
- [ ] Home, nothing playing. BLOCKED: Open question 9
- [ ] Music menu with preview pane
- [ ] Artists, Albums, Songs, Genres, Compilations, an artist, a genre
- [ ] Two-line song rows
- [ ] Fast-scroll tile and index rail in both styles
- [ ] Long names: tickers verified with the owner's real album names (README Open question 11)

## Phase 6: albums

- [ ] An artist's albums: thumbnail, year, song count (Open questions 6 and 7)
- [ ] Album header in both styles
- [ ] Cover Flow, Modern (flat five)
- [ ] Cover Flow, Classic. BLOCKED until phase 0 says the angled version is affordable (Open question 8)
- [ ] Modern recolouring per album in Cover Flow and Album (Open question 5)

## Phase 7: everything else, in both styles

- [ ] Settings root and summary pane
- [ ] Settings > Display, Click, Haptics, Speaker / Output
- [ ] About, Diagnostics, Licences
- [ ] Bluetooth: settings page, Devices, device options, pairing, questions
- [ ] Wi-Fi: list, password entry, status
- [ ] Files
- [ ] Playlists and On-The-Go
- [ ] Wheel input: text, password, number, slider, choice, date and time
- [ ] Library building, empty and error states
- [ ] Hold: lock pill, power-hold line, power-off countdown
- [ ] Boot splash
- [ ] Test tone and bench screens

## Phase 8: finish

- [ ] New styles are the default where the build carries them
- [ ] Full pass: every screen, every style, every size, on host
- [ ] Full pass on each attached board, recorded with the date
- [ ] `docs/ui.md` rewritten to match what was built
- [ ] This folder's README updated where the build differs, with reasons
- [ ] Final report

## Notes

(One line per thing the next session must know. Newest first.)
