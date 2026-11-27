// saved_state.h
// Remembers where the tour was, so it can pick up there after the board is
// switched off (even by pulling the power).
//
// Saved: the stop that was loaded (by FILE NAME, so adding or reordering
// recordings never resumes the wrong one), the position in it, the volume,
// and the highlighted stop. Stored as a small text file, e.g.
//     track=05_AQ.mp3
//     position=123.4
//     volume=70
//     highlighted=05_AQ.mp3
//
// Writes are atomic: the new contents go to "<file>.tmp", are flushed to the
// SD card, and then renamed over the old file. A power cut at any moment
// leaves either the old or the new file, never a half-written one.

#ifndef _SAVED_STATE_H_
#define _SAVED_STATE_H_

#include <stdbool.h>

#define SAVED_STATE_NAME_CHARS 256
#define SAVED_STATE_INTERVAL_MS 5000   // while playing, save the position this often

typedef struct {
    char track[SAVED_STATE_NAME_CHARS];        // "" = nothing loaded
    double position_sec;
    int volume_percent;                        // -1 = unknown
    char highlighted[SAVED_STATE_NAME_CHARS];  // "" = unknown
} saved_state_t;

void saved_state_clear(saved_state_t *state);

// Returns false if the file is missing or unreadable (state is then cleared).
bool saved_state_load(const char *path, saved_state_t *state);

// Atomic write (see above). Returns false on failure.
bool saved_state_save(const char *path, const saved_state_t *state);

// Should `now_state` be written, given what was last written and how long ago?
//   - a different stop, volume or highlight: yes, at once
//   - only the position moved (by 1 s or more): yes, once SAVED_STATE_INTERVAL_MS passed
bool saved_state_should_save(const saved_state_t *last_saved, const saved_state_t *now_state,
                             long long ms_since_last_save);

#endif
