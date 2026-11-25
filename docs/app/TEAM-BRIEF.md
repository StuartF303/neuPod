# Team brief: build the neuPod phone app (R1)

You are the **lead** of a small team of coding subagents. Together you build and test release R1 of the app specified in
[README.md](README.md): a local music player for iOS and Android, in Flutter, in `mobile/` at the repository root.

Read the specification in full, then the [UI styles design](../design/2026-11-21-ui-styles/README.md) and its mock-ups,
then `CONTRIBUTING.md`. [PROGRESS.md](PROGRESS.md) is the only record of where the work stands: a new session, or one
whose context was compacted, starts by reading it and carries on from the first unticked item.

R2 (NAS) and R3 (companion) are out of scope. Do not start them.

## Where to run

iOS can only be built and tested on macOS, so run this on the owner's Mac. Before anything else, check and report:
`flutter doctor -v`, Xcode and its command-line tools, CocoaPods, an iOS simulator, the Android SDK and an emulator, and
`gh` signed in. Install what is missing where that is routine; ask the owner before changing anything system-wide. If
you are not on a Mac, say so and stop: do not build Android alone and call it done.

`scripts/app/bootstrap-mac.sh` installs the base toolchain (Xcode check, Flutter, CocoaPods, a JDK, the Android
command-line tools) and starts you in a detached `tmux` session named `neupod-app`; `scripts/app/run-agent.sh` restarts
you. Keep both working as the project's needs change.

## The team

You plan, delegate, integrate and decide. You do not write feature code yourself. Give each subagent one item, the parts
of the specification that govern it, the files it owns, and what "done" means. Run independent items in parallel; keep
one owner per file at a time.

| Role | Owns |
| --- | --- |
| **Architect** | Project scaffold, layering, package choices and their decision notes, CI |
| **Library engineer** | Folder access on both platforms, scanner, tag reading and fallbacks, database, art cache |
| **Audio engineer** | Player, gapless, queue, background playback, media session, interruptions, output route |
| **UI engineer** (one or two) | Styles and skins, components, screens, navigation, accessibility |
| **Test engineer** | Fixtures, the generated libraries, golden and integration tests, the audio and scale tests |
| **Reviewer** | A fresh subagent for every PR that has not seen the work; reads the diff against the specification |

The test engineer writes the tests for an item from the specification, not from the implementation.

## Rules

- The app shares one player UI with every neuPod device ([ADR-0022](../adr/0022-one-player-ui.md)): the firmware is
  the reference, and the app differs only where the phone's hardware or OS requires it.
- Work only in `mobile/`, `docs/app/`, `scripts/app/`, the app's CI workflow, and the `LICENSE.md` rows the app needs. **Do not change
  the firmware, the hardware files or their CI.**
- Branches, commits and PRs follow `CONTRIBUTING.md`, with scope `app`. Small PRs. Squash-merge when CI is green.
  Never force-push.
- The repository is public. Never commit signing keys, provisioning profiles, keystores, passwords, API tokens, the
  owner's music, or anything personal. Add the ignore rules first.
- R1 makes no network connection. No analytics, crash reporting service or advertising SDK, ever.
- The app never writes to, moves or deletes a music file.
- Choose packages from their current documentation and release history, not from memory. Record each choice and the
  alternatives in `docs/app/decisions/`. Keep the in-app Licences screen and `LICENSE.md` current.
- No "iPod", no Apple marks, and no artwork copied from Apple's products, anywhere in the app or its listing.
- Do not submit anything to a store or to TestFlight. The owner does that.

## The loop

Repeat until R1 is done:

1. **Pick** the next unticked items in `PROGRESS.md` whose phase is open. Take as many as can run in parallel without
   sharing files.
2. **Brief** a subagent for each, and the test engineer for its tests.
3. **Integrate**: both platforms build; unit, widget, golden and integration tests pass on the Android emulator and the
   iOS simulator; static analysis is clean; formatting applied.
4. **Review** with a fresh reviewer. Fix what it finds or record why not.
5. **PR**, CI green, merge.
6. **Tick** the items in `PROGRESS.md` with the PR number and date. Add a line under Notes for anything the next
   session must know.
7. Go to 1.

At the end of each phase, run the device checks in the specification's test table on the owner's iPhone, and on an
Android phone if one is attached, and record what was verified, on what, with the date. Steps that need the owner's
hands (trusting the developer certificate, plugging in a phone, a signing prompt) are asked for once, in a batch.

## Gates: stop and show the owner

1. **After phase 1 (the vertical slice).** Stop. Show the app running on both platforms: pick a folder, scan, a Songs
   list, play, lock the phone, control from the lock screen. Report the package choices.
2. **The design gate, in phase 2.** Before building every screen, build Home, a list, an album, Cover Flow and Now
   Playing in portrait, in Classic Light and Modern Dark, with real data. Where the specification leaves the portrait
   layout open, build the two best candidates. Stop and show screenshots side by side. Build the rest only after the
   owner chooses.
3. **Before R1 is called done.** The final report, below.

Also stop and ask when: an item depends on one of the specification's Open questions; the specification is silent or
contradicts itself on something that changes what the user sees; a platform cannot do what the specification requires
(for example a format it cannot decode, or sample-rate passthrough); a package you depend on turns out unmaintained or
wrongly licensed; or the same item has failed three times.

When you stop, leave `main` green and `PROGRESS.md` current, and say exactly what you need.

## Phases

**Phase 0: set-up.** Environment report. Reconcile `PROGRESS.md` with the specification and add anything missing.
Scaffold `mobile/`, the ignore rules, CI (Android on Linux, iOS on a Mac runner, no signing), the decision notes for
the audio engine, media session, tag reader, database and state management.

**Phase 1: vertical slice.** One thin path through every layer on both platforms: choose a folder, scan it, list the
songs, play one, keep playing in the background with lock-screen controls. Plain styling. Then gate 1.

**Phase 2: look and feel.** Style and mode tokens from the UI styles design, the skins, the core components, the
design-gate screens. Then gate 2.

**Phase 3: library.** Full scanner with tag fallbacks and the folder convention, discs, compilations, incremental
rescan, art cache, ratings and play counts, the scale test.

**Phase 4: playback.** Queue, gapless, shuffle and repeat, seek, interruptions, output route, resume, all formats with
the audio tests.

**Phase 5: screens.** Every browsing screen, search, fast scroll, Cover Flow, Now Playing, the queue, in both styles
and modes.

**Phase 6: playlists and settings.** On-The-Go, named playlists, `.m3u8` import and export, Settings, Licences, About.

**Phase 7: finish.** Accessibility pass, performance against the targets, an hour of continuous play on each device, a
full pass of every screen in every style and mode, the user-facing `mobile/README.md` with build and install steps.

## Done

Every item in `PROGRESS.md` is ticked or marked `BLOCKED` with an owner question attached, CI is green on `main`, and
the final report lists: what was built; the measured numbers against each target; what was verified on which device and
when; what is blocked and on what; every package used with its licence; and what R2 and R3 will need from R1 that is
not there yet.
