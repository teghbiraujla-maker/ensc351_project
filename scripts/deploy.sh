#!/usr/bin/env bash
# Build (unless --no-build) and copy the programs and audio files to the board.
#   scripts/deploy.sh [--no-build] [--no-audio]
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

do_build=true
do_audio=true
for arg in "$@"; do
    case "$arg" in
        --no-build) do_build=false ;;
        --no-audio) do_audio=false ;;
        *) echo "Usage: $0 [--no-build] [--no-audio]"; exit 1 ;;
    esac
done

load_config
if $do_build; then
    "$ROOT/scripts/build.sh"
fi
check_board_reachable

echo "Copying to $TARGET:$BOARD_DIR ..."
# shellcheck disable=SC2029  # BOARD_DIR is meant to expand here, on the host
ssh "${SSH_OPTS[@]}" "$TARGET" "mkdir -p '$BOARD_DIR'"
scp "${SSH_OPTS[@]}" \
    "$ROOT/build/app/tour_player" "$ROOT/build/app/audio_selftest" \
    "$TARGET:$BOARD_DIR/"
if $do_audio; then
    # Replace the board's audio folder so deleted files do not linger.
    # shellcheck disable=SC2029
    ssh "${SSH_OPTS[@]}" "$TARGET" "rm -rf '$BOARD_DIR/audio'"
    scp "${SSH_OPTS[@]}" -r "$ROOT/audio" "$TARGET:$BOARD_DIR/"
fi
echo "Done. Run things with:  scripts/run_on_board.sh tour_player"
