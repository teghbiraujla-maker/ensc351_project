// tour_progress.h
// Saves where the tour is (which stop, how far in) and resumes it after a
// restart or power cut. Glue between tour_controller and progress_store.
//
//   main.c:  tour_progress_init(path);   tour_progress_resume();   (at start-up)
//            tour_progress_update();     (every main-loop frame, after tour_controller_tick)
//            tour_progress_save_now();   (before a hold-to-stop, and before exiting)
//
// What gets saved, and when:
//   - every TOUR_PROGRESS_SAVE_INTERVAL_SEC while a stop is playing or paused
//   - immediately when a stop starts, pauses or resumes
//   - when the user holds to stop (so the exact spot is kept) and on a normal quit
//   - the file is cleared when a stop FINISHES by itself (nothing to resume)
// A sudden power-off therefore loses at most a few seconds.
//
// Not thread-safe: call from the main loop only.

#ifndef _TOUR_PROGRESS_H_
#define _TOUR_PROGRESS_H_

#include <stdbool.h>

#define TOUR_PROGRESS_SAVE_INTERVAL_SEC 5.0

// path = progress file (see progress_store_default_path). NULL disables saving.
// Call after tour_controller_init().
void tour_progress_init(const char *path);

// Continue the saved stop at the saved spot. Returns true if it resumed.
// A missing, corrupt or out-of-date file just returns false (tour starts fresh).
bool tour_progress_resume(void);

// Call once per frame, after tour_controller_tick().
void tour_progress_update(void);
void tour_progress_update_at(double now_sec);   // same, with an explicit clock (for tests)

// Save the current stop and position right now (does nothing while choosing).
void tour_progress_save_now(void);

#endif
