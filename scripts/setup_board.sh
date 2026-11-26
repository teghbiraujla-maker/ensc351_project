#!/usr/bin/env bash
# One-time setup of a board: installs mpg123 and checks libgpiod / USB audio.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

load_config
check_board_reachable
# shellcheck disable=SC2029  # BOARD_DIR is meant to expand here, on the host
ssh "${SSH_OPTS[@]}" "$TARGET" "mkdir -p '$BOARD_DIR'"
scp "${SSH_OPTS[@]}" "$ROOT/scripts/board_setup.sh" "$TARGET:$BOARD_DIR/"
# -t so that sudo can ask for the board's password
ssh -t "${SSH_OPTS[@]}" "$TARGET" "bash '$BOARD_DIR/board_setup.sh'"
