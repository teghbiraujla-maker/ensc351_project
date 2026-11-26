#!/usr/bin/env bash
# Run one of the programs on the board (with sudo, which GPIO access for the encoder needs).
#   scripts/run_on_board.sh tour_player
#   scripts/run_on_board.sh audio_selftest
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <tour_player|audio_selftest> [program arguments]"
    exit 1
fi
program="$1"
shift

load_config
# printf %q quotes each argument so it survives the trip through ssh.
quoted_args=""
if [ $# -gt 0 ]; then
    quoted_args="$(printf ' %q' "$@")"
fi
# -t gives the program a real terminal (needed for the interactive menu and sudo's password prompt)
ssh -t "${SSH_OPTS[@]}" "$TARGET" "cd '$BOARD_DIR' && sudo ./$program$quoted_args"
