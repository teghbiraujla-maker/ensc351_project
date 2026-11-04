// audio_device.h
// HAL module: work out which ALSA output device to use.
//
// Boards have several sound cards (HDMI, USB adapter, ...) and the card
// numbers change between boards and reboots, so rather than hard-coding
// "hw:1,0" we look for the USB audio adapter in /proc/asound/cards.

#ifndef _AUDIO_DEVICE_H_
#define _AUDIO_DEVICE_H_

#include <stdbool.h>
#include <stddef.h>

// Scan `cards_file` (normally /proc/asound/cards) for the first USB audio
// card and write an ALSA device string such as "plughw:1,0" into `out`.
// Returns false (and leaves `out` unchanged) if there is no USB audio card.
bool audio_device_find_usb(const char *cards_file, char *out, size_t out_size);

// Decide which device to play on:
//   requested == NULL or "auto"  -> the USB audio adapter if present
//   anything else                -> used exactly as given (e.g. "plughw:2,0")
// If nothing suitable is found, `out` is set to "default".
// Returns true if the device was explicitly requested or a USB card was
// found, false if it fell back to "default".
bool audio_device_resolve(const char *requested, char *out, size_t out_size);

#endif
