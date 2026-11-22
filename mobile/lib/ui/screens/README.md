# ui/screens: the device's screens

What runs inside the device view's 600x450 screen. `shell/` is the navigation the screens plug into; `browse/` is the
browsing half of the design gate (Home, Music, Songs, Artists, an artist, Albums, an album, Cover Flow, Genres, a genre,
Compilations, and a small Settings). Now Playing, the panel's content and Search are built by UI engineer B on top of
`shell/`.

## The shell (`shell/`)

| File | What |
| --- | --- |
| `shell_state.dart` | `shellProvider` (the stack and the panel), `ShellRegistry` / `shellRegistryProvider`, `ShellDestination` |
| `shell_root.dart` | `ShellRoot(home: ...)`: the stack, the 80 ms slide, wheel routing, the panel layer, `bus.atRoot` |
| `screen_scope.dart` | `ScreenScope` / `ScreenHandle`: `isTop` and the wheel handler of a screen |
| `shell_frame.dart` | `ShellFrame`: canvas + status bar + body, in the chosen style, mode and text tier |
| `shell_style.dart` | `styleChoiceProvider` (style, mode), `deviceStatusProvider`, `ambientPaletteProvider`, `artPaletteProvider` |
| `neupod_list.dart` | `NeupodList`: rows, the sliding selection bar, wheel, touch, scroll bar, letter tile |
| `cover_art.dart` | `CoverArt(ArtRef)`: the cover file or the mock-ups' split-square placeholder |
| `stand_in_screen.dart` | A greyed "Not built yet" screen, shown until a destination registers |

### For Now Playing, the panel and Search (UI engineer B)

1. **Register your screens** once, before the first frame (an override of `shellRegistryProvider`, or in `NeuPodApp`):
   `ref.read(shellRegistryProvider).register(ShellDestination.nowPlaying, (context) => const NowPlayingScreen())`, and the
   same for `ShellDestination.search`. Until then the destinations open a stand-in. Home's Now Playing item and "Select on
   a song" open `ShellDestination.nowPlaying`; the Music menu's last item opens `ShellDestination.search`.
2. **Open and close.** `ref.read(shellProvider.notifier)`: `open(destination)`, `push(ShellPage(name:, builder:))`,
   `pop()`, `popToRoot()`, `showPanel(title:, message:, okLabel:)` (returns when dismissed; Select, Menu or a tap on OK).
3. **Build your screen with `ShellFrame(title: ..., context: ..., palette: ..., backdrop: ..., body: ...)`.** It is
   `NeupodScreen` plus the status bar (play state from the player, battery and output from `deviceStatusProvider`) in
   the style and mode the owner chose. Pass `palette` (an `ArtPalette`) when the screen shows an album of its own, as
   Cover Flow and an album do; otherwise Modern uses `ambientPaletteProvider`, the album of the song playing.
   `BackdropLayout.nowPlaying` is yours.
4. **Take the wheel.** In `didChangeDependencies`, `ScreenScope.maybeOf(context)` gives the screen's `ScreenHandle`:
   set `handle.wheel = (WheelEvent e) => ...` (return true when you used the event), clear it in `dispose` (only if it
   is still yours), and listen to `handle.isTop`: set `bus.semantics` (and `bus.holdMs`, `bus.screenGivesHoldHaptic`)
   when it turns true, and take your additions away when it turns false or you go. The shell reads the bus once;
   do not listen to `wheelBusProvider.events` yourself, or a screen under yours would hear the events too.
   What a screen does not take, the shell does: Menu pops, Previous, Next and Play/Pause drive the player. Search
   takes Menu itself (delete a character, clear on hold, leave on an empty field) and returns true.
5. **Lists** are `NeupodList(items: [...])` of `NeupodListItem.row(...)` (or `bandItem` for a heading the wheel skips).
   It does the wheel, the 80 ms bar, the whole-rows window, touch candidate 2, the scroll bar and the fast-scroll tile.
   Search's results can use it.
6. `bus.atRoot` is set by the shell (Home alone and no panel). Do not set it.

## What the browsing screens do

- **Home**: Music, Playlists (stand-in), Settings, Shuffle Songs, and Now Playing only while the player has a current
  item. The right pane shows the cover, title, artist and progress of what is loaded, or the placeholder, `Nothing
  playing` and the song count (not drawn by the design; UI styles Open question 9).
- **Music**: Cover Flow, Playlists, Artists, Albums, Songs, Genres, Compilations, Search. The pane previews the item.
- **Songs / All Songs**: two lines per row, the count in the status bar. **Select plays the song and queues the list it
  was chosen from** (`playFrom` in `browse/browse_common.dart`): the songs before it stay for Previous; greyed songs
  are left out of the queue; Now Playing opens at once.
- **Artists** then an artist: All Songs, then albums with cover, year and song count in year order. **Albums**,
  **Genres** then a genre (All Songs, then its albums), **Compilations**.
- **An album**: header (cover, artist, `year · N songs · total time`), then numbered tracks; `Disc n` headings only when
  the album has several discs; a track that cannot play is greyed with the reason at its right (and skipped by the
  wheel); on a compilation each track shows its own artist at the right.
- **Cover Flow**: the Albums list's albums in its order (owner decision 14); Classic angled with reflections, Modern
  flat; the wheel moves a cover per step, Select or a tap on the centre cover opens it, a drag moves the covers. Under
  the title, in both styles, the device's caption `Artist · Year · n songs` (`flow_texts` in `app/ui/ui_library.c`).
- **Settings**: Style (Classic or Modern) and Mode (Auto, Light, Dark) only; the rest is phase 6.

## Where the data comes from

`app/library_view_providers.dart` provides `libraryViewProvider` (a `LibraryView`, `domain/library_view/`): the adapter
over the scanned drift database, `data/library_view/drift_library_view.dart`, sorted by the firmware collation. What the
database does not hold yet (album artist, year, disc, genre, compilation flag, art) shows as empty Genres and
Compilations lists and placeholder covers until phase 3. The screens' tests and goldens and the gate-2 tool use
`test/support/manifest_library_view.dart`, a test-only fake over the generated test library's manifest.
