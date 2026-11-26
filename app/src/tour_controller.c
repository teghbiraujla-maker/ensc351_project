#include "tour_controller.h"
#include <assert.h>
#include <stdio.h>
#include "app_log.h"
#include "playback_display.h"
#include "track_list.h"

static const tour_player_t *player = NULL;
static int highlighted = -1;
static int current = -1;
static bool tour_complete = false;
static bool user_stopped = true;              // the last stop was on purpose (hold)
static audio_state_t previous_state = AUDIO_STOPPED;
static bool nextIsHighlighted = true;
static void play_highlighted(void); // two additions made by TA

static tour_mode_t mode_for(audio_state_t state)
{
    switch (state) {
    case AUDIO_PLAYING: return TOUR_MODE_PLAYING;
    case AUDIO_PAUSED:  return TOUR_MODE_PAUSED;
    default:            return TOUR_MODE_CHOOSING;
    }
}

void tour_controller_init(const tour_player_t *player_functions)
{
    assert(player_functions != NULL);
    player = player_functions;
    highlighted = (track_list_count() > 0) ? 0 : -1;
    current = -1;
    tour_complete = false;
    user_stopped = true;
    previous_state = player->get_status().state;
    nextIsHighlighted = true;
}

// ---------------------------------------------------------------------------
// Turning
// ---------------------------------------------------------------------------
static void turn_to_play(int steps) {
    int count = track_list_count();
    if (count == 0) {
        app_log("No tracks to choose from. Copy MP3 files into the audio folder.");
        return;
    }
    if (highlighted < 0) {
        highlighted = 0;
        nextIsHighlighted = true;
    }
    bool plays_highlighted = nextIsHighlighted && steps > 0;
    if (plays_highlighted) {
        steps -= 1;
    }
    int target = highlighted + steps;
    if (target < 0) target = 0;
    if (target > count - 1) target = count - 1;

    if (target == highlighted && !plays_highlighted) {
        app_log(steps > 0 ? "That is the last stop." : "That is the first stop.");
        return;
    }
    highlighted = target;
    nextIsHighlighted = false;
    tour_complete = false;
    play_highlighted();
}

static void change_volume(int steps)
{
    player->adjust_volume(steps * TOUR_VOLUME_STEP_PERCENT);
    app_log("Volume %d%%", player->get_status().volume_percent);
}

// Skip while paused, never before 0:00 or past (end - TOUR_END_GUARD_SEC).
//   target = position + steps * TOUR_SEEK_STEP_SEC, clamped to [0, duration - guard]
//   jump   = target - position, in whole seconds (mpg123 jumps in seconds)
static void skip(int steps)
{
    audio_status_t status = player->get_status();
    double position = status.position_sec;
    double target = position + (double)steps * TOUR_SEEK_STEP_SEC;
    int jump;

    if (target <= 0.0) {
        int whole = (int)position;             // round UP, so the jump reaches 0:00
        if (position > (double)whole) whole++;
        jump = -whole;
    } else if (status.duration_sec > 0.0) {
        double last_allowed = status.duration_sec - TOUR_END_GUARD_SEC;
        if (last_allowed < 0.0) last_allowed = 0.0;
        if (target > last_allowed) target = last_allowed;
        jump = (int)(target - position);       // truncates toward 0: stays inside the limits
    } else {
        jump = steps * TOUR_SEEK_STEP_SEC;     // length unknown: plain relative jump
    }

    if (jump == 0) {
        app_log(steps > 0 ? "At the end of the recording. Hold to stop, or press to resume."
                          : "At the start of the recording.");
        return;
    }
    player->seek_relative(jump);

    audio_status_t after = player->get_status();
    char time_text[32];
    playback_display_time(after.position_sec, after.duration_sec, time_text, sizeof(time_text));
    app_log("Skipped %s %d s: %s", jump > 0 ? "forward" : "back", jump > 0 ? jump : -jump, time_text);
}

// ---------------------------------------------------------------------------
// Pressing
// ---------------------------------------------------------------------------
static void play_highlighted(void)
{
    if (highlighted < 0) {
        app_log("No tracks to play. Copy MP3 files into the audio folder.");
        return;
    }
    if (player->play(track_list_path(highlighted))) {
        current = highlighted;
        user_stopped = false;
        tour_complete = false;
        previous_state = AUDIO_PLAYING;   // so tick() reports it if it ends (or fails) right away
        app_log("Playing %d. %s", current + 1, track_list_name(current));
    } else {
        char reason[200];
        player->get_last_error(reason, sizeof(reason));
        app_log("Could not play '%s': %s", track_list_name(highlighted), reason);
    }
}

static void toggle_pause(void)
{
    if (player->toggle_pause()) {
        app_log("%s", player->get_status().state == AUDIO_PAUSED
                          ? "Paused. Turn to skip, press to resume, hold to stop."
                          : "Resumed.");
    }
}

static void stop_and_choose(void)
{
    user_stopped = true;
    player->stop();
    previous_state = AUDIO_STOPPED;
    if (current >= 0) {
        highlighted = current;
    }
    nextIsHighlighted = false;
    app_log("Stopped. Turn to choose a stop, press to play.");
}

void tour_controller_handle(control_event_t event)
{
    assert(player != NULL);
    tour_mode_t mode = mode_for(player->get_status().state);

    switch (event.type) {
    case CONTROL_TURN:
        if (event.steps == 0) {
            break;
        }
        if (mode == TOUR_MODE_CHOOSING) {
            turn_to_play(event.steps);
        } else if (mode == TOUR_MODE_PLAYING) {
            change_volume(event.steps);
        } else {
            skip(event.steps);
        }
        break;
    case CONTROL_PRESS:
        if (mode == TOUR_MODE_CHOOSING) {
            play_highlighted();
        } else {
            toggle_pause();
        }
        break;
    case CONTROL_LONG_PRESS:
        if (mode != TOUR_MODE_CHOOSING) {
            stop_and_choose();
        }
        break;
    default:
        break;   // debug commands are handled by main.c
    }
}

// ---------------------------------------------------------------------------
// Track ending by itself
// ---------------------------------------------------------------------------
void tour_controller_tick(void)
{
    assert(player != NULL);
    audio_state_t state = player->get_status().state;

    if (previous_state != AUDIO_STOPPED && state == AUDIO_STOPPED && !user_stopped) {
        user_stopped = true;
        char reason[200];
        player->get_last_error(reason, sizeof(reason));
        if (reason[0] != '\0') {
            app_log("Playback problem: %s", reason);
            highlighted = current;
            nextIsHighlighted = false;
        } else if (current + 1 < track_list_count()) {
            highlighted = current + 1;
            nextIsHighlighted = true;
            app_log("Finished: %s. Next stop: %d. %s - press to play.",
                    track_list_name(current), highlighted + 1, track_list_name(highlighted));
        } else {
            highlighted = current;
            tour_complete = true;
            nextIsHighlighted = true;
            app_log("Finished: %s. That was the last stop - thanks for taking the tour!",
                    track_list_name(current));
        }
    }
    previous_state = state;
}

tour_view_t tour_controller_view(void)
{
    assert(player != NULL);
    tour_view_t view;
    view.audio = player->get_status();
    view.mode = mode_for(view.audio.state);
    switch (view.mode) {
    case TOUR_MODE_PLAYING: view.focus = TOUR_FOCUS_VOLUME;     break;
    case TOUR_MODE_PAUSED:  view.focus = TOUR_FOCUS_PROGRESS;   break;
    default:                view.focus = TOUR_FOCUS_TRACK_LIST; break;
    }
    view.track_count = track_list_count();
    view.highlighted = highlighted;
    view.current = current;
    view.tour_complete = tour_complete;
    return view;
}

const char *tour_controller_mode_name(tour_mode_t mode)
{
    switch (mode) {
    case TOUR_MODE_PLAYING: return "PLAYING";
    case TOUR_MODE_PAUSED:  return "PAUSED";
    default:                return "CHOOSING";
    }
}
