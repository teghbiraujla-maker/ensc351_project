#!/usr/bin/env bash
# Run one of the programs on the board (with sudo, which GPIO and SPI need).
#   scripts/run_on_board.sh tour_player
#   scripts/run_on_board.sh audio_selftest
#   scripts/run_on_board.sh joystick_test
#   scripts/run_on_board.sh display_test [--spi-hz 1000000]
# If the tour-player service is running, it is stopped first.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <tour_player|audio_selftest|joystick_test|display_test> [program arguments]"
    exit 1
fi
program="$1"
shift

load_config
stop_service_if_running || true
# printf %q quotes each argument so it survives the trip through ssh.
quoted_args=""
if [ $# -gt 0 ]; then
    quoted_args="$(printf ' %q' "$@")"
fi
# -t gives the program a real terminal (needed for the interactive menu and sudo's password prompt)
ssh -t "${SSH_OPTS[@]}" "$TARGET" "cd '$BOARD_DIR' && sudo ./$program$quoted_args"
