// progress_store.h
// Remembers where the tour stopped (which recording and how far into it) so
// the next start can continue from there.
//
// Safety rules this module follows:
//  - Saving writes a temporary file, flushes it to disk, then renames it over
//    the real file. A power cut mid-save leaves the OLD file intact.
//  - Loading validates everything. A missing, empty, truncated or corrupted
//    file just means "no saved progress" - it can never crash the player.
//  - The recording is stored by FILE NAME (e.g. "03_Convocation_Mall.mp3"),
//    not by number, so adding or removing recordings cannot point at the wrong one.
//  - No hardware or audio needed, so it is covered by a host unit test.

#ifndef _PROGRESS_STORE_H_
#define _PROGRESS_STORE_H_

#include <stdbool.h>
#include <stddef.h>

#define PROGRESS_TRACK_FILE_MAX 256

typedef struct {
    char track_file[PROGRESS_TRACK_FILE_MAX];   // file name only, no folders
    double position_sec;                        // how far into the recording
} saved_progress_t;

// Where the progress file lives: $TOUR_PROGRESS_FILE if set, otherwise
// $HOME/.sfu_tour_progress, otherwise "sfu_tour_progress.txt" in the current folder.
void progress_store_default_path(char *out, size_t out_size);

// Returns false (and leaves any existing file untouched) if it cannot save.
bool progress_store_save(const char *path, const char *track_file, double position_sec);

// Returns true only if the file exists and is completely valid.
bool progress_store_load(const char *path, saved_progress_t *out);

// Forget the saved progress (e.g. the tour recording finished).
void progress_store_clear(const char *path);

#endif
