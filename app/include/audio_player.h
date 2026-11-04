// audio_player.h
// Plays MP3 files through the Linux audio subsystem (ALSA).
//
// Implementation: this module starts the `mpg123` player in its "remote
// control" mode and talks to it through pipes. A background thread reads the
// player's status messages so the rest of the program can simply ask
// audio_player_get_status(). All functions are thread-safe.
//
// The interface does not expose any mpg123 details, so the implementation
// could later be swapped (e.g. for libmpg123 + ALSA) without changing callers.

#ifndef _AUDIO_PLAYER_H_
#define _AUDIO_PLAYER_H_

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    AUDIO_STOPPED,   // nothing playing (never started, stopped, or finished)
    AUDIO_PLAYING,
    AUDIO_PAUSED,
} audio_state_t;

typedef struct {
    audio_state_t state;
    double position_sec;    // how far into the recording we are
    double duration_sec;    // total length (0 until known)
    int volume_percent;     // 0..100
} audio_status_t;

// Start the player process on the given ALSA device (e.g. "plughw:1,0";
// NULL or "default" = the system default). Returns false (reason printed to
// stderr) if the player cannot be started, e.g. mpg123 is not installed.
bool audio_player_init(const char *alsa_device);

// Stop playback and shut the player process down.
void audio_player_cleanup(void);

// Start playing an MP3 file, replacing anything already playing.
// Returns false if the file cannot be read or the player is not running.
bool audio_player_play(const char *mp3_path);

// Pause if playing, resume if paused. Returns false if nothing is loaded.
bool audio_player_toggle_pause(void);

void audio_player_stop(void);

// Volume is 0..100 (values outside are clamped).
void audio_player_set_volume(int percent);
void audio_player_adjust_volume(int delta_percent);

// Jump forward (positive) or back (negative) within the current recording.
void audio_player_seek_relative(int seconds);

// Snapshot of the current state.
audio_status_t audio_player_get_status(void);

// Copy the most recent error/diagnostic text from the player into `out`
// (empty string if none). Useful for telling the user why playback failed.
void audio_player_get_last_error(char *out, size_t out_size);

#endif
