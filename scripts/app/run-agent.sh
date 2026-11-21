#!/bin/bash
# SPDX-License-Identifier: MPL-2.0
# neuPod phone app: start the lead coding agent on the team brief. Run from anywhere in
# the repository, normally inside the tmux session that bootstrap-mac.sh creates.
# Extra arguments are passed to the agent, for example a permission setting.
set -u
cd "$(dirname "$0")/../.." || exit 1
for b in /opt/homebrew/bin/brew /usr/local/bin/brew; do
  [ -x "$b" ] && eval "$("$b" shellenv)" && break
done
export ANDROID_HOME="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
export PATH="$ANDROID_HOME/platform-tools:$ANDROID_HOME/emulator:$PATH"

PROMPT="Read docs/app/TEAM-BRIEF.md and follow it exactly. You are the lead of a team of coding \
subagents building release R1 of the app specified in docs/app/README.md. If this checkout is on a \
branch other than main because the brief has not merged yet (PR #100, branch docs/phone-app-spec), \
merge that PR first once CI is green, then switch to main and pull. The base toolchain was installed \
by scripts/app/bootstrap-mac.sh. Start with 'Where to run' and phase 0. Work through \
docs/app/PROGRESS.md in a loop, delegating items to subagents and running independent ones in \
parallel. Stop at each gate and whenever the brief says to ask me, and tell me exactly what you need."

claude "$@" "$PROMPT"
echo
echo "The agent has exited. Start it again with: scripts/app/run-agent.sh"
