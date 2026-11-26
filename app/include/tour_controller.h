// tour_controller.h
// The encoder-only control scheme. Turns CONTROL_TURN / PRESS / LONG_PRESS
// events into player actions, depending on what is happening:
//
//   Mode       | Turn                         | Press         | Hold (~1 s)
//   -----------+------------------------------+---------------+------------------
//   CHOOSING   | move highlight (stops at the | play the      | -
//   (stopped)  | first / last stop)           | highlighted   |
//   PLAYING    | volume +/- 5% per click      | pause         | stop, back to list
//   PAUSED     | skip +/- 5 s per click       | resume        | stop, back to list
//              | (never past end - 1 s)       |               |
//
// When a track finishes by itself, the NEXT stop is highlighted (nothing
// starts on its own); one press plays it.
//
// The GUI calls tour_controller_view() every frame and draws from it: it
// says what mode we are in and which control the encoder is driving
// ("focus"), so the matching widget can be highlighted.
//
// Not thread-safe: call everything from the main loop only.

#ifndef _TOUR_CONTROLLER_H_
#define _TOUR_CONTROLLER_H_

#include <stdbool.h>
#include <stddef.h>
#include "audio_player.h"
#include "controls.h"

#define TOUR_VOLUME_STEP_PERCENT 5
#define TOUR_SEEK_STEP_SEC       5
#define TOUR_END_GUARD_SEC       1    // skipping forward stops this far before the end

typedef enum {
    TOUR_MODE_CHOOSING,    // nothing playing: pick a stop
    TOUR_MODE_PLAYING,
    TOUR_MODE_PAUSED,
} tour_mode_t;

// What turning the encoder changes right now (= what the GUI should highlight).
typedef enum {
    TOUR_FOCUS_TRACK_LIST,
    TOUR_FOCUS_VOLUME,
    TOUR_FOCUS_PROGRESS,
} tour_focus_t;

// Everything the GUI needs for one frame.
typedef struct {
    tour_mode_t mode;
    tour_focus_t focus;
    int track_count;
    int highlighted;        // 0-based highlighted stop in the list; -1 if no tracks
    int current;            // 0-based stop playing / paused / last played; -1 if none yet
    bool tour_complete;     // the last stop finished playing
    audio_status_t audio;   // state, position, duration, volume
} tour_view_t;

// The player functions the controller uses. main.c passes the real
// audio_player_* functions; the unit test passes a fake player.
typedef struct {
    audio_status_t (*get_status)(void);
    bool (*play)(const char *mp3_path);
    bool (*toggle_pause)(void);
    void (*stop)(void);
    void (*adjust_volume)(int delta_percent);
    void (*seek_relative)(int seconds);
    void (*get_last_error)(char *out, size_t out_size);
} tour_player_t;

// Call after track_list_load(). The player struct must stay valid.
void tour_controller_init(const tour_player_t *player);

// React to an encoder event (TURN, PRESS, LONG_PRESS). Other events are ignored.
void tour_controller_handle(control_event_t event);

// Call once per main-loop iteration: notices when a track ends by itself.
void tour_controller_tick(void);

tour_view_t tour_controller_view(void);

const char *tour_controller_mode_name(tour_mode_t mode);

#endif
