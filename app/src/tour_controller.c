#include "tour_controller.h"
#include <assert.h>
#include <stdio.h>
#include "app_log.h"
#include "guide_text.h"
#include "playback_display.h"
#include "track_list.h"

static const tour_player_t *player = NULL;
static int highlighted = -1;
static int current = -1;                       // stop loaded, or last one loaded
static bool user_stopped = true;               // last stop was on purpose (hold)
static audio_state_t previous_state = AUDIO_STOPPED;
static tour_popup_t popup = TOUR_POPUP_NONE;
static int popup_stop = -1;
static bool scrubbing = false;
static double scrub_target = 0.0;

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
    user_stopped = true;
    previous_state = player->get_status().state;
    popup = TOUR_POPUP_NONE;
    popup_stop = -1;
    scrubbing = false;
}

void tour_controller_set_highlight(int index)
{
    if (index >= 0 && index < track_list_count()) {
        highlighted = index;
    }
}

bool tour_controller_resume(int track_index, double position_sec)
{
    assert(player != NULL);
    const char *path = track_list_path(track_index);
    if (path == NULL || !player->load_paused(path, position_sec)) {
        return false;
    }
    current = track_index;
    highlighted = track_index;
    user_stopped = false;
    previous_state = player->get_status().state;
    char time_text[32];
    audio_status_t status = player->get_status();
    playback_display_time(status.position_sec, status.duration_sec, time_text, sizeof(time_text));
    app_log("Resumed where the tour left off: %d. %s at %s (paused - press the knob to play).",
            track_index + 1, track_list_name(track_index), time_text);
    return true;
}

// ---------------------------------------------------------------------------
// Encoder
// ---------------------------------------------------------------------------
static void move_highlight(int steps)
{
    int count = track_list_count();
    if (count == 0) {
        app_log("No tracks to choose from. Copy MP3 files into the audio folder.");
        return;
    }
    int target = highlighted + steps;
    if (target < 0) target = 0;                 // stops at the ends (no wrap-around)
    if (target > count - 1) target = count - 1;
    highlighted = target;
    app_log("Highlighted stop %d/%d: %s", highlighted + 1, count, track_list_name(highlighted));
}

static void play_highlighted(void)
{
    if (highlighted < 0) {
        app_log("No tracks to play. Copy MP3 files into the audio folder.");
        return;
    }
    if (player->play(track_list_path(highlighted))) {
        current = highlighted;
        user_stopped = false;
        previous_state = AUDIO_PLAYING;   // so tick() reports it if it ends (or fails) right away
        app_log("Playing %d. %s", current + 1, track_list_name(current));
    } else {
        char reason[200];
        player->get_last_error(reason, sizeof(reason));
        app_log("Could not play '%s': %s", track_list_name(highlighted), reason);
    }
}

// ---------------------------------------------------------------------------
// Joystick skipping: preview while held, one jump on release.
//   target = start position + seconds held x 10, clamped to [0, duration - guard]
//   jump   = target - position now, in whole seconds (mpg123 jumps in seconds)
// ---------------------------------------------------------------------------
static double clamp_target(double target, double duration)
{
    if (target < 0.0) target = 0.0;
    if (duration > 0.0) {
        double last_allowed = duration - TOUR_END_GUARD_SEC;
        if (last_allowed < 0.0) last_allowed = 0.0;
        if (target > last_allowed) target = last_allowed;
    }
    return target;
}

static void seek_step(int seconds)
{
    audio_status_t status = player->get_status();
    if (!scrubbing) {
        scrubbing = true;
        scrub_target = status.position_sec;
    }
    scrub_target = clamp_target(scrub_target + seconds, status.duration_sec);
}

static void finish_scrub(void)
{
    if (!scrubbing) {
        return;
    }
    scrubbing = false;
    audio_status_t status = player->get_status();
    if (status.state == AUDIO_STOPPED) {
        return;
    }
    double target = clamp_target(scrub_target, status.duration_sec);
    double position = status.position_sec;
    int jump;
    if (target <= 0.0) {
        int whole = (int)position;              // round UP, so the jump reaches 0:00
        if (position > (double)whole) whole++;
        jump = -whole;
    } else {
        jump = (int)(target - position);        // truncates toward 0: stays inside the limits
    }
    if (jump == 0) {
        return;
    }
    player->seek_relative(jump);
    audio_status_t after = player->get_status();
    char time_text[32];
    playback_display_time(after.position_sec, after.duration_sec, time_text, sizeof(time_text));
    app_log("Skipped %s %d s: %s", jump > 0 ? "forward" : "back", jump > 0 ? jump : -jump, time_text);
}

// ---------------------------------------------------------------------------
// Popups
// ---------------------------------------------------------------------------
static void close_popup(void)
{
    popup = TOUR_POPUP_NONE;
    popup_stop = -1;
    app_log("Directions closed. Turn the knob to choose a stop.");
}

static bool open_guide(int stop)
{
    char text[GUIDE_TEXT_MAX];
    if (!guide_text_get(stop, text, sizeof(text))) {
        return false;
    }
    popup = TOUR_POPUP_GUIDE;
    popup_stop = stop;
    app_log("Directions to stop %d. %s: %s", stop + 1, track_list_name(stop), text);
    return true;
}

static void toggle_guide(void)
{
    if (popup != TOUR_POPUP_NONE) {
        close_popup();
    } else if (highlighted >= 0 && !open_guide(highlighted)) {
        app_log("Stop 1 is where the tour begins - no directions needed.");
    }
}

// ---------------------------------------------------------------------------
void tour_controller_handle(control_event_t event)
{
    assert(player != NULL);
    tour_mode_t mode = mode_for(player->get_status().state);
    bool loaded = (mode != TOUR_MODE_CHOOSING);

    // A skip that was started finishes even if a popup appeared meanwhile.
    if (event.type == CONTROL_SEEK_RELEASE) {
        finish_scrub();
        return;
    }
    if (event.type == CONTROL_GUIDE) {
        finish_scrub();
        toggle_guide();
        return;
    }
    if (popup != TOUR_POPUP_NONE) {
        // Only pause / resume of a loaded track works behind a popup.
        if (event.type == CONTROL_PRESS && loaded) {
            finish_scrub();
            if (player->toggle_pause()) {
                app_log("%s", player->get_status().state == AUDIO_PAUSED ? "Paused." : "Resumed.");
            }
        } else if (event.type == CONTROL_TURN || event.type == CONTROL_PRESS ||
                   event.type == CONTROL_LONG_PRESS || event.type == CONTROL_VOLUME ||
                   event.type == CONTROL_SEEK_STEP) {
            app_log("Press the joystick to close the directions first.");
        }
        return;
    }

    switch (event.type) {
    case CONTROL_TURN:
        if (event.steps != 0) {
            move_highlight(event.steps);
        }
        break;
    case CONTROL_PRESS:
        if (!loaded) {
            play_highlighted();
        } else {
            finish_scrub();
            if (player->toggle_pause()) {
                app_log("%s", player->get_status().state == AUDIO_PAUSED ? "Paused." : "Resumed.");
            }
        }
        break;
    case CONTROL_LONG_PRESS:
        if (loaded) {
            scrubbing = false;
            user_stopped = true;
            player->stop();
            previous_state = AUDIO_STOPPED;
            app_log("Track ended. Turn the knob to choose a stop, press to play.");
        }
        break;
    case CONTROL_VOLUME:
        if (event.steps != 0) {
            player->adjust_volume(event.steps * TOUR_VOLUME_STEP_PERCENT);
            app_log("Volume %d%%", player->get_status().volume_percent);
        }
        break;
    case CONTROL_SEEK_STEP:
        if (loaded && event.steps != 0) {
            seek_step(event.steps);
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
        scrubbing = false;
        char reason[200];
        player->get_last_error(reason, sizeof(reason));
        if (reason[0] != '\0') {
            app_log("Playback problem: %s", reason);
            highlighted = current;
        } else if (current + 1 < track_list_count()) {
            highlighted = current + 1;
            app_log("Finished: %s. Next stop: %d. %s", track_list_name(current),
                    highlighted + 1, track_list_name(highlighted));
            open_guide(highlighted);
        } else {
            highlighted = current;
            popup = TOUR_POPUP_TOUR_COMPLETE;
            popup_stop = -1;
            app_log("Finished: %s. Tour complete - thanks for taking the tour!",
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
    view.track_count = track_list_count();
    view.highlighted = highlighted;
    view.playing = (view.mode != TOUR_MODE_CHOOSING) ? current : -1;
    view.scrubbing = scrubbing && view.mode != TOUR_MODE_CHOOSING;
    view.position_sec = view.scrubbing ? scrub_target : view.audio.position_sec;
    view.popup = popup;
    view.popup_stop = popup_stop;
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
