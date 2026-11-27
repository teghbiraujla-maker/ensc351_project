// track_list.h
// Finds the MP3 files for the tour and gives each a number and a friendly name.
//
// Files are sorted by file name, so name them with a numeric prefix to control
// the order:  01_Welcome.mp3, 02_Library.mp3, 03_Convocation_Mall.mp3 ...
// The displayed name drops the number, the extension and the underscores.

#ifndef _TRACK_LIST_H_
#define _TRACK_LIST_H_

#include <stdbool.h>

// Scan `directory` for .mp3 files. Returns false if the directory cannot be
// read. (An empty directory succeeds with a count of 0.)
bool track_list_load(const char *directory);

int track_list_count(void);

// index is 0-based. Both return NULL if the index is out of range.
const char *track_list_name(int index);   // e.g. "Convocation Mall"
const char *track_list_path(int index);   // full path to the file

// File name only, e.g. "03_Convocation_Mall.mp3" (NULL if out of range).
const char *track_list_file_name(int index);

// Index of the track with this file name, or -1 if there is none.
int track_list_find(const char *file_name);

void track_list_cleanup(void);

#endif
