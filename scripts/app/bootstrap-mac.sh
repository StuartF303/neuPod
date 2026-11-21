#!/bin/bash
# SPDX-License-Identifier: MPL-2.0
# neuPod phone app: check and install the macOS toolchain, then start the coding agent
# detached in tmux so it survives the ssh session. Safe to run again.
#
#   scripts/app/bootstrap-mac.sh            # toolchain, then start the agent
#   scripts/app/bootstrap-mac.sh --no-agent # toolchain only
#
# Runs in the foreground until everything needed is in place, so a missing piece is
# reported where you can see it. Only the agent is detached.
set -u
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
SESSION=neupod-app
LOG="$HOME/neupod-app.log"

say() { printf '\n=== %s\n' "$*"; }
die() { printf '\nSTOPPED: %s\n' "$*" >&2; exit 1; }

[ "$(uname -s)" = Darwin ] || die "This script is for macOS."

for b in /opt/homebrew/bin/brew /usr/local/bin/brew; do
  [ -x "$b" ] && eval "$("$b" shellenv)" && break
done
command -v brew >/dev/null || die "Homebrew is missing. Install it from https://brew.sh (needs your password), then run this again."

say "Xcode"
xcodebuild -version || die "Full Xcode is missing. Install it from the App Store, open it once, then run this again."
xcodebuild -checkFirstLaunchStatus || die "Run: sudo xcodebuild -license accept && sudo xcodebuild -runFirstLaunch   then run this again."

say "Homebrew packages"
brew install git gh tmux cocoapods || die "brew install failed."
brew install --cask flutter temurin@17 android-commandlinetools || die "brew install --cask failed."

say "Android SDK"
export ANDROID_HOME="$HOME/Library/Android/sdk"
mkdir -p "$ANDROID_HOME"
SDKM="$(command -v sdkmanager)" || die "sdkmanager not found after installing android-commandlinetools."
yes | "$SDKM" --sdk_root="$ANDROID_HOME" --licenses >/dev/null
"$SDKM" --sdk_root="$ANDROID_HOME" "platform-tools" "emulator" || die "sdkmanager failed."
flutter config --android-sdk "$ANDROID_HOME" --no-analytics >/dev/null
yes | flutter doctor --android-licenses >/dev/null 2>&1

say "flutter doctor (the agent installs what is still missing, such as the Android platform)"
flutter doctor -v

say "GitHub"
gh auth status || die "GitHub CLI is not signed in. Run: gh auth login   then run this again."

[ "${1:-}" = "--no-agent" ] && { say "Toolchain ready. Agent not started (--no-agent)."; exit 0; }

say "Coding agent"
command -v claude >/dev/null || die "The 'claude' command is not on PATH. Install Claude Code and sign in, then run this again."
if tmux has-session -t "$SESSION" 2>/dev/null; then
  say "A '$SESSION' session is already running. Attach: tmux attach -t $SESSION"
  exit 0
fi
# A plain shell session that the agent is typed into: the window stays open if the agent exits.
tmux new-session -d -s "$SESSION" -c "$REPO" || die "tmux could not start a session."
tmux pipe-pane -t "$SESSION" -o "cat >> '$LOG'"
tmux send-keys -t "$SESSION" "scripts/app/run-agent.sh" Enter

say "Started, detached. It keeps running if ssh drops."
echo "  Attach:  tmux attach -t $SESSION      (detach again: Ctrl-b then d)"
echo "  Log:     tail -f $LOG"
echo "  Stop:    tmux kill-session -t $SESSION"
