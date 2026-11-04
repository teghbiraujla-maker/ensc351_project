#!/usr/bin/env bash
# Cross-compile the project for the BeagleY-AI. Output goes to build/app/.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

if ! command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 || ! command -v cmake >/dev/null 2>&1; then
    echo "Missing build tools. Install them with:"
    echo "  sudo apt install cmake gcc-aarch64-linux-gnu"
    exit 1
fi

# CMAKE_EXTRA_ARGS (optional) comes from config.sh
CMAKE_EXTRA_ARGS=""
# shellcheck source=/dev/null
[ -f "$CONFIG_FILE" ] && source "$CONFIG_FILE"
read -r -a extra_args <<< "${CMAKE_EXTRA_ARGS:-}"

cmake -S "$ROOT" -B "$ROOT/build" \
      -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/aarch64-toolchain.cmake" \
      "${extra_args[@]}"
cmake --build "$ROOT/build" -j"$(nproc)"
echo
echo "Built: $ROOT/build/app/{tour_player,joystick_test,audio_selftest}"
