# UI mock-ups

Target: the Phase 1 panel, **320×240 landscape**. Each screen is an original drawing at the
panel's native size (SVG, `viewBox="0 0 320 240"`); the look follows the classic click-wheel
player layout — split menu with artwork, blue selection bar — lightly modernised. These are
design intent, not screenshots of any existing firmware.

Click-wheel vocabulary used below: **Menu** (top button, goes back), **Select** (centre),
**Play/Pause** (bottom), **Left** / **Right** (previous / next), **scroll** (finger on the ring).

## Main menu

![Main menu](assets/ui/main-menu.svg)

- Scroll moves the selection bar through Music, Podcasts, Photos, Extras, Settings, Shuffle
  Songs, Now Playing. Scroll acceleration follows the Rockbox constants (4 counts per step,
  velocity-based acceleration).
- **Select** opens the highlighted item. **Menu** does nothing at the top level.
- **Play/Pause** toggles playback without leaving the menu; the right pane shows the current
  track's artwork and title, or a clock when nothing is playing.
- **Left** / **Right** skip tracks in the background.

## Library — artists

![Artists list](assets/ui/library-artists.svg)

- Full-width list; scroll bar on the right shows position in long lists. Fast scrolling shows a
  letter overlay (not drawn) so a 100k-track library is navigable.
- **Select** descends (artist → albums → tracks); **Menu** ascends one level.
- **Right** on an artist jumps to All Albums for that artist; **Left** returns to the previous
  level without animation.
- The same template serves Albums, Songs, Genres, Composers, Playlists and Podcasts.

## Now playing

![Now playing](assets/ui/now-playing.svg)

- Shows artwork, title, artist, album, the format badge (e.g. FLAC 24/96, wired, bit-perfect)
  and the progress bar with elapsed and remaining time.
- Scroll adjusts **volume** by default; a first **Select** switches the scroll to **seek** (bar
  highlights), a second Select returns to volume. Pressing Select again shows rating / shuffle /
  repeat options (not drawn).
- **Play/Pause** toggles; **Left** / **Right** previous / next track (hold Right to fast-forward).
- **Menu** returns to the list the track was chosen from.
- On Bluetooth output the badge reads the negotiated codec (e.g. SBC-XQ, AAC).

## Album browser

![Album browser](assets/ui/album-browser.svg)

- Dark full-screen carousel of album artwork; the centre album is enlarged with a reflection.
- Scroll flips through albums with acceleration; **Select** opens the centre album's track
  list; **Menu** returns to the library.
- This view is the one place the UI uses the panel's colour depth heavily; on the AMOLED
  candidate the black background also saves power.

## Settings

![Settings](assets/ui/settings.svg)

- Two-pane: list of settings groups on the left, a live detail pane on the right for the
  highlighted group (shown: Bluetooth — connected device, codec, battery).
- **Select** enters a group; inside, scroll changes the value and Select confirms.
- "Click feedback" is where the piezo / haptic / off choice lives, pending the hardware decision
  in [Requirements → Reused parts and mechanical](requirements.md#reused-parts-and-mechanical).
- "Output" chooses headphone / line / optical / USB when the jack detect cannot decide.

## Not yet drawn

- Hold-switch lock overlay, charging screen, USB mass-storage ("Connected") screen.
- Podcasts and Photos browsers; the search screen.
- Letter overlay during fast scroll; rating / shuffle / repeat sub-screens.
