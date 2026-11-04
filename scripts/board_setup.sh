#!/usr/bin/env bash
# Runs ON THE BOARD (scripts/setup_board.sh copies it over and runs it).
# Installs what the programs need and checks the hardware.
set -u

echo "== 1. Installing the audio player and sound tools (needs internet on the board) =="
sudo apt-get update || echo "(apt update reported problems; trying the install anyway)"
sudo apt-get install -y mpg123 alsa-utils

echo
echo "== 2. Checking SPI (needed for the joystick's ADC) =="
if [ -e /dev/spidev0.0 ]; then
    echo "OK: /dev/spidev0.0 exists."
else
    echo "PROBLEM: /dev/spidev0.0 does not exist. SPI is not enabled on this board."
    echo "         Follow the course's SPI guide to enable it, then reboot."
fi

echo
echo "== 3. Checking for the USB audio adapter =="
if grep -q "USB-Audio" /proc/asound/cards 2>/dev/null; then
    echo "OK: USB audio adapter found:"
    grep -A1 "USB-Audio" /proc/asound/cards
else
    echo "PROBLEM: no USB audio adapter found. Is it plugged into one of the board's USB-A ports?"
fi

echo
echo "== Sound cards the board can see (aplay -l) =="
aplay -l 2>&1 || true
