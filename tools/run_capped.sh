#!/usr/bin/env bash
# Run a test binary, oracle or tool the way every test must be run here:
#
# 1. Under a hard memory cap, so a runaway allocation is killed (exit 137)
#    instead of exhausting the machine. The cap is on resident memory, not
#    address space, so it works with ASan builds too.
# 2. Hermetically, like the TypeScript project's test preload
#    (../quake-anthology-ts/tests/support/hermetic.ts): HOME and the XDG
#    directories point at a throwaway directory, and QTS_TEST_GAME_WRITE_ROOT
#    redirects the writes a stock game makes beside its game data (config
#    files, logs, demos, saves, downloads). Without this an application opened
#    over the real corpus writes into the game install, and content loading
#    migrates demos and saves out of the developer's real profile.
#    The oracle scripts a test starts inherit the same environment, so both
#    sides see the same empty profile.
#
# Usage: tools/run_capped.sh [-m 6G] command [args...]
#   QA_MEMORY_CAP=8G     default cap (6G), no swap
#   QA_TEST_HERMETIC=0   keep the real HOME (only for tools that need it)
#   QTS_TEST_CORPUS      game data root; defaults to the real ~/qfiles
set -euo pipefail
cap=${QA_MEMORY_CAP:-6G}
if [ "${1:-}" = "-m" ]; then cap=$2; shift 2; fi
if [ "${QA_TEST_HERMETIC:-1}" = 1 ]; then
  repo=$(cd "$(dirname "$0")/.." && pwd)
  real_home=$HOME
  mkdir -p "$repo/build/test-runs"
  run=$(mktemp -d "$repo/build/test-runs/run-XXXXXX")
  trap 'rm -rf "$run"' EXIT
  mkdir -p "$run/home/.local/share" "$run/home/.config" "$run/home/.local/state" "$run/tmp"
  # Tests that default the corpus to ~/qfiles keep working; it is only read:
  # game-folder writes go to QTS_TEST_GAME_WRITE_ROOT.
  if [ -e "$real_home/qfiles" ]; then ln -s "$real_home/qfiles" "$run/home/qfiles"; fi
  export QTS_TEST_CORPUS=${QTS_TEST_CORPUS:-$real_home/qfiles}
  export HOME=$run/home
  export XDG_DATA_HOME=$HOME/.local/share XDG_CONFIG_HOME=$HOME/.config XDG_STATE_HOME=$HOME/.local/state
  export QTS_TEST_GAME_WRITE_ROOT=${QTS_TEST_GAME_WRITE_ROOT:-$run/game-writes}
  export TMPDIR=$run/tmp
  # Tests must never open the machine's audio output.
  export SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-dummy}
fi
systemd-run --user --scope -q -p MemoryMax="$cap" -p MemorySwapMax=0 -- "$@"
