// tour_controller.h
// The tour's control rules. Turns control events (controls.h) into player
// actions and keeps the state the GUI draws.
//
//   Encoder turn        move the highlight in the list (never changes what is playing)
//   Encoder press       pause / resume; if nothing is loaded, play the highlighted stop
//   Encoder hold (1 s)  end the current track (nothing else)
//   Joystick up / down  volume +/- 5% (repeats every 0.5 s while held)
//   Joystick right/left skip forward / back: while held, the position bar previews
//                       the new spot (10 s per second held); releasing the stick
//                       jumps there once. Never before 0:00 or past end - 1 s.
//   Joystick press      show the directions to the highlighted stop / close them
//
// When a track ends by itself, the next stop is highlighted and its
// directions pop up; after the last stop a "tour complete" popup shows.
// While a popup is open, only the joystick press (close) and the encoder
// press (pause / resume a loaded track) do anything.
//
// Not thread-safe: call everything from the main loop only.

#ifndef _TOUR_CONTROLLER_H_
#define _TOUR_CONTROLLER_H_

#include <stdbool.h>
#include <stddef.h>
#include "audio_player.h"
#include "controls.h"

#define TOUR_VOLUME_STEP_PERCENT 5
#define TOUR_END_GUARD_SEC       1    // skipping forward stops this far before the end

typedef enum {
    TOUR_MODE_CHOOSING,    // nothing loaded: pick a stop
    TOUR_MODE_PLAYING,
    TOUR_MODE_PAUSED,
} tour_mode_t;

typedef enum {
    TOUR_POPUP_NONE,
    TOUR_POPUP_GUIDE,          // directions to popup_stop
    TOUR_POPUP_TOUR_COMPLETE,  // the last stop finished
} tour_popup_t;

// Everything the GUI needs for one frame.
typedef struct {
    tour_mode_t mode;
    int track_count;
    int highlighted;          // 0-based stop selected with the encoder; -1 if no tracks
    int playing;              // 0-based stop loaded (playing or paused); -1 if none
    bool scrubbing;           // joystick held left/right
    double position_sec;      // position to SHOW (the preview while scrubbing)
    tour_popup_t popup;
    int popup_stop;           // 0-based stop the guide popup is for
    audio_status_t audio;     // the player's real state, position, duration, volume
} tour_view_t;

// The player functions the controller uses. main.c passes the real
// audio_player_* functions; the unit test passes a fake player.
typedef struct {
    audio_status_t (*get_status)(void);
    bool (*play)(const char *mp3_path);
    bool (*load_paused)(const char *mp3_path, double position_sec);
    bool (*toggle_pause)(void);
    void (*stop)(void);
    void (*adjust_volume)(int delta_percent);
    void (*seek_relative)(int seconds);
    void (*get_last_error)(char *out, size_t out_size);
} tour_player_t;

// Call after track_list_load(). The player struct must stay valid.
void tour_controller_init(const tour_player_t *player);

// Start-up after a restart: load a stop PAUSED at position_sec (returns false
// if it could not be loaded), and/or move the highlight.
bool tour_controller_resume(int track_index, double position_sec);
void tour_controller_set_highlight(int index);

// React to an input event. Debug commands (help, status ...) are ignored.
void tour_controller_handle(control_event_t event);

// Call once per main-loop iteration: notices when a track ends by itself.
void tour_controller_tick(void);

tour_view_t tour_controller_view(void);

const char *tour_controller_mode_name(tour_mode_t mode);

#endif
