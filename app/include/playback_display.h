// playback_display.h
// Turns the audio player's position/duration into text and numbers for the
// screen (or terminal). No hardware, no threads: pure formatting, so it is
// easy to unit test.
//
// The real timing comes from audio_player_get_status() (position_sec and
// duration_sec, reported by mpg123), so pause, seek and track end are always
// accurate - nothing here counts seconds on its own.

#ifndef _PLAYBACK_DISPLAY_H_
#define _PLAYBACK_DISPLAY_H_

#include <stddef.h>
#include "audio_player.h"

// One time: "M:SS" (e.g. "3:24"), or "H:MM:SS" from one hour up.
void playback_display_clock(double seconds, char *out, size_t out_size);

// Position and length: "M:SS/M:SS" (e.g. "2:34/3:24"). If the track is an
// hour or longer, both use "H:MM:SS". The position never shows past the end.
void playback_display_time(double position_sec, double duration_sec,
                           char *out, size_t out_size);

// How far through the track, 0.0 .. 1.0, for the progress bar
// (0.0 while the length is unknown).
double playback_display_progress(const audio_status_t *status);

// "PLAYING", "PAUSED" or "STOPPED".
const char *playback_display_state_text(audio_state_t state);

// One status line, e.g. "3 Convocation Mall  2:34/3:24 PLAYING".
// track_number is 1-based; pass 0 (or name NULL) if nothing is selected.
void playback_display_line(const audio_status_t *status, int track_number,
                           const char *track_name, char *out, size_t out_size);

#endif
