// playback_display.c
#include "playback_display.h"

#include <stdio.h>

#define SECONDS_PER_HOUR 3600

// Seconds -> clock text:
//   hours   = total / 3600
//   minutes = (total % 3600) / 60
//   seconds = total % 60          (%02d pads to two digits)
static void format_clock(int total, int show_hours, char *out, size_t out_size)
{
    if (show_hours) {
        snprintf(out, out_size, "%d:%02d:%02d", total / SECONDS_PER_HOUR,
                 (total % SECONDS_PER_HOUR) / 60, total % 60);
    } else {
        snprintf(out, out_size, "%d:%02d", total / 60, total % 60);
    }
}

void playback_display_clock(double seconds, char *out, size_t out_size)
{
    int total = (seconds > 0) ? (int)seconds : 0;   // truncate, like a video player
    format_clock(total, total >= SECONDS_PER_HOUR, out, out_size);
}

void playback_display_time(double position_sec, double duration_sec,
                           char *out, size_t out_size)
{
    if (position_sec < 0) position_sec = 0;
    if (duration_sec < 0) duration_sec = 0;
    int pos = (int)position_sec;
    int dur = (int)duration_sec;
    if (dur > 0 && pos > dur) pos = dur;
    int show_hours = (dur >= SECONDS_PER_HOUR || pos >= SECONDS_PER_HOUR);

    char pos_text[16];
    char dur_text[16];
    format_clock(pos, show_hours, pos_text, sizeof(pos_text));
    format_clock(dur, show_hours, dur_text, sizeof(dur_text));
    snprintf(out, out_size, "%s/%s", pos_text, dur_text);
}

double playback_display_progress(const audio_status_t *status)
{
    if (status->duration_sec <= 0) {
        return 0.0;
    }
    double fraction = status->position_sec / status->duration_sec;
    if (fraction < 0.0) return 0.0;
    if (fraction > 1.0) return 1.0;
    return fraction;
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
