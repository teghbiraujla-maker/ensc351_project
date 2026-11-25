// playback_display.c
#include "playback_display.h"

#include <stdio.h>

// Seconds -> minutes and seconds:
//   minutes = total / 60   (0-59 -> 0, 60-119 -> 1, 120-179 -> 2 ...)
//   seconds = total % 60   (%02d pads to two digits)
void playback_display_time(double position_sec, double duration_sec,
                           char *out, size_t out_size)
{
    if (position_sec < 0) position_sec = 0;
    if (duration_sec < 0) duration_sec = 0;
    int pos = (int)position_sec;      // truncate, like a video player
    int dur = (int)duration_sec;
    if (dur > 0 && pos > dur) pos = dur;
    snprintf(out, out_size, "%d:%02d/%d:%02d", pos / 60, pos % 60, dur / 60, dur % 60);
}

const char *playback_display_state_text(audio_state_t state)
{
    switch (state) {
    case AUDIO_PLAYING: return "PLAYING";
    case AUDIO_PAUSED:  return "PAUSED";
    default:            return "STOPPED";
    }
}

void playback_display_line(const audio_status_t *status, int track_number,
                           const char *track_name, char *out, size_t out_size)
{
    if (track_number < 1 || track_name == NULL || status->state == AUDIO_STOPPED) {
        snprintf(out, out_size, "%s", playback_display_state_text(status->state));
        return;
    }
    char time_text[24];
    playback_display_time(status->position_sec, status->duration_sec,
                          time_text, sizeof(time_text));
    snprintf(out, out_size, "%d %s  %s %s", track_number, track_name, time_text,
             playback_display_state_text(status->state));
}
