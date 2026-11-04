#!/usr/bin/env bash
# Shared setup for the helper scripts (sourced, not run directly).

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG_FILE="$ROOT/scripts/config.sh"

load_config() {
    if [ ! -f "$CONFIG_FILE" ]; then
        echo "No scripts/config.sh yet."
        echo "Create it with:  cp scripts/config.sh.example scripts/config.sh"
        echo "then edit BOARD_USER and BOARD_HOST (see README.md)."
        exit 1
    fi
    # shellcheck source=/dev/null
    source "$CONFIG_FILE"
    : "${BOARD_USER:?BOARD_USER is not set in scripts/config.sh}"
    : "${BOARD_HOST:?BOARD_HOST is not set in scripts/config.sh}"
    BOARD_DIR="${BOARD_DIR:-campus_tour}"
    TARGET="${BOARD_USER}@${BOARD_HOST}"
    # Accept the board's host key the first time instead of stopping to ask.
    SSH_OPTS=(-o StrictHostKeyChecking=accept-new)
}

check_board_reachable() {
    if ! ssh "${SSH_OPTS[@]}" -o ConnectTimeout=5 "$TARGET" true; then
        echo
        echo "Cannot reach $TARGET."
        echo "  - Is the board powered on and booted (give it ~60 s)?"
        echo "  - Is BOARD_USER / BOARD_HOST in scripts/config.sh correct?"
        echo "  - Try it by hand:  ssh $TARGET"
        exit 1
    fi
}
