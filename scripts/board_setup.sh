#!/usr/bin/env bash
# Runs ON THE BOARD (scripts/setup_board.sh copies it over and runs it).
# Installs what the programs need and checks the hardware.
set -u

echo "== 1. Installing the audio player and sound tools (needs internet on the board) =="
sudo apt-get update || echo "(apt update reported problems; trying the install anyway)"
sudo apt-get install -y mpg123 alsa-utils

echo
echo "== 2. Checking libgpiod v2 (needed for the encoder, joystick button and screen) =="
if ldconfig -p 2>/dev/null | grep -q "libgpiod.so.3"; then
    echo "OK: libgpiod v2 runtime library found."
else
    echo "PROBLEM: libgpiod v2 (libgpiod.so.3) not found. The encoder, joystick button and screen will not work."
    echo "         Try:  sudo apt-get install libgpiod3   (package name may differ on your image)"
fi

echo
echo "== 3. Checking SPI (joystick ADC on spidev0.0, screen on spidev0.1) =="
for dev in /dev/spidev0.0 /dev/spidev0.1; do
    if [ -e "$dev" ]; then
        echo "OK: $dev exists."
    else
        echo "PROBLEM: $dev does not exist. Enable SPI (course SPI guide, section 1), then reboot."
    fi
done

echo
echo "== 4. Checking for the USB audio adapter =="
if grep -q "USB-Audio" /proc/asound/cards 2>/dev/null; then
    echo "OK: USB audio adapter found:"
    grep -A1 "USB-Audio" /proc/asound/cards
else
    echo "PROBLEM: no USB audio adapter found. Is it plugged into one of the board's USB-A ports?"
fi

echo
echo "== Sound cards the board can see (aplay -l) =="
aplay -l 2>&1 || true
