# ADR-0022: One player UI on every device and app

Status: **accepted** (owner decision 24 Nov 2026)

## Context

neuPod runs on several targets: the Pico 2 W rig, the Waveshare RP2350 and ESP32-S3 AMOLED dev boards, the Y2 testbed and the
desktop simulator, with the final RP2350B board to come. It also runs as a phone app (`mobile/`, iOS and Android). Each of
these differs in screen size, input, memory and platform services. Without a rule, each port drifts into its own UI. The
phone app already follows a narrower version of this rule ("firmware behaviour is law", `docs/app/TEAM-BRIEF.md`). This ADR
makes it the rule for the whole project.

## Decision

* **There is one neuPod UI.** Every device and every app shares the same player: screens, menus and navigation, the
  wheel and its acceleration, the letter jump, list contents and sort order, the text shown, Now Playing, album art choice
  and Cover Flow. Someone who knows one neuPod knows them all.
* **The firmware is the reference.** `app/ui`, `app/audio` and `app/library` define the behaviour. Other implementations,
  such as the phone app, port it exactly and check the port against the compiled C (vectors, snapshots). Where the firmware
  is silent, the owner decides, and the decision is written down for every target.
* **Differences are allowed only for hardware or platform reasons**, and only at the edges of the player:
  * screen size and density: the resolution profiles of [ADR-0018](0018-ui-resolution-profiles.md);
  * input: click wheel, touch, side keys;
  * memory: for example, the frozen Pico 2 W keeps a 64 x 64 art thumbnail and has no Cover Flow;
  * radios and peripherals present: Wi-Fi, Bluetooth, battery gauge;
  * platform services: lock-screen controls, notifications, OS permissions, where the music lives.
* **A difference is a setting or a build option, not a redesign.** It is named where it lives (a CMake option, a setting,
  a platform adapter) and documented on that target's page. It changes what is available, not how the player behaves.
  A feature the hardware cannot support is left out or greyed out. It is not replaced by a different design.
* **Changes land everywhere.** A change to the player UI goes into the firmware and every app that ports it, or the gap
  is recorded as a backlog item for each target that lacks it. A gap with no hardware reason is a bug to close, not a
  difference to keep.
* **App-only surfaces use the device's look.** Where an app needs something the device has no screen for (a folder
  picker, a permission prompt, an OS setting), it uses the device's visual language and wording.

## Consequences

* **RAM budget:** none. This is a rule about behaviour; each feature's ADR still carries its own budget, and memory is a
  valid reason for a difference.
* Reviews of UI changes check every target, not just the one the change was made on. A PR that changes the player on one
  target names the matching change or backlog item for the others.
* Known gap today: the ESP32-S3 and RP2350 AMOLED dev-board builds use the Pico's small art thumbnail and have no
  Cover Flow, because `NEUPOD_ART_HIRES` is on only for hosted builds. Where a board has the memory (PSRAM), this is a
  gap to close, not a hardware difference.
