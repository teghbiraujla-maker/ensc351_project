// tour_progress.c
#include "tour_progress.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "app_log.h"
#include "progress_store.h"
#include "tour_controller.h"
#include "track_list.h"

static char progress_path[1024];
static bool enabled = false;
static bool warned = false;
static tour_mode_t last_mode = TOUR_MODE_CHOOSING;
static double last_save_time = 0.0;
static bool finish_check_pending = false;

void tour_progress_init(const char *path)
{
    enabled = (path != NULL && path[0] != '\0');
    if (enabled) {
        snprintf(progress_path, sizeof(progress_path), "%s", path);
    }
    warned = false;
    last_mode = tour_controller_view().mode;
    last_save_time = 0.0;
    finish_check_pending = false;
}

static const char *file_name_of(const char *path)
{
    const char *slash = (path != NULL) ? strrchr(path, '/') : NULL;
    return (slash != NULL) ? slash + 1 : path;
}

// Index of the stop with this file name, or -1 if it is no longer in the folder.
static int find_track_by_file(const char *file_name)
{
    for (int i = 0; i < track_list_count(); i++) {
        const char *name = file_name_of(track_list_path(i));
        if (name != NULL && strcmp(name, file_name) == 0) {
            return i;
        }
    }
    return -1;
}

bool tour_progress_resume(void)
{
    saved_progress_t saved;
    if (!enabled || !progress_store_load(progress_path, &saved)) {
        return false;
    }
    int index = find_track_by_file(saved.track_file);
    if (index < 0) {
        app_log("Saved stop '%s' is no longer in the audio folder - starting fresh.", saved.track_file);
        return false;
    }
    bool resumed = tour_controller_resume(index, saved.position_sec);
    if (resumed) {
        last_mode = tour_controller_view().mode;   // so a stop ending right away is still noticed
    }
    return resumed;
}

void tour_progress_save_now(void)
{
    if (!enabled) {
        return;
    }
    tour_view_t view = tour_controller_view();
    if (view.mode == TOUR_MODE_CHOOSING || view.current < 0) {
        return;
    }
    const char *name = file_name_of(track_list_path(view.current));
    // A failed save is never fatal: the tour keeps playing; warn once.
    if (name != NULL && !progress_store_save(progress_path, name, view.audio.position_sec) && !warned) {
        app_log("Warning: could not save tour progress to '%s'.", progress_path);
        warned = true;
    }
}

void tour_progress_update_at(double now_sec)
{
    if (!enabled) {
        return;
    }
    tour_view_t view = tour_controller_view();

    if (view.mode != TOUR_MODE_CHOOSING) {
        finish_check_pending = false;
        if (view.mode != last_mode || now_sec - last_save_time >= TOUR_PROGRESS_SAVE_INTERVAL_SEC) {
            tour_progress_save_now();
            last_save_time = now_sec;
        }
    } else if (last_mode != TOUR_MODE_CHOOSING) {
        finish_check_pending = true;           // just went back to choosing: decide next frame
    } else if (finish_check_pending) {
        // By now tour_controller_tick() has handled the end of the stop. A stop that
        // FINISHED by itself highlights the next stop (or marks the tour complete);
        // a stop the user held to stop, or a playback error, leaves it highlighted.
        finish_check_pending = false;
        if (view.tour_complete || view.highlighted != view.current) {
            progress_store_clear(progress_path);
        }
    }
    last_mode = view.mode;
}

void tour_progress_update(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    tour_progress_update_at((double)now.tv_sec + (double)now.tv_nsec / 1e9);
}
