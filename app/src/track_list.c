#include "track_list.h"
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    char file_name[NAME_MAX + 1];
    char display_name[NAME_MAX + 1];
    char *path;
} track_t;

static track_t *tracks = NULL;
static int track_count = 0;

static bool has_mp3_extension(const char *name)
{
    size_t length = strlen(name);
    return length > 4 && strcasecmp(name + length - 4, ".mp3") == 0;
}

// "03_Convocation_Mall.mp3" -> "Convocation Mall"
static void make_display_name(const char *file_name, char *out, size_t out_size)
{
    const char *start = file_name;
    while (*start >= '0' && *start <= '9') {
        start++;
    }
    while (*start == '_' || *start == '-' || *start == ' ' || *start == '.') {
        start++;
    }
    if (*start == '\0') {          // name was only digits: keep it
        start = file_name;
    }

    snprintf(out, out_size, "%s", start);
    size_t length = strlen(out);
    if (length > 4) {
        out[length - 4] = '\0';    // drop ".mp3"
    }
    for (char *c = out; *c != '\0'; c++) {
        if (*c == '_') {
            *c = ' ';
        }
    }
}

static int compare_tracks(const void *a, const void *b)
{
    return strcmp(((const track_t *)a)->file_name, ((const track_t *)b)->file_name);
}

bool track_list_load(const char *directory)
{
    track_list_cleanup();

    DIR *dir = opendir(directory);
    if (dir == NULL) {
        return false;
    }

    int capacity = 16;
    tracks = calloc((size_t)capacity, sizeof(track_t));
    if (tracks == NULL) {
        closedir(dir);
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!has_mp3_extension(entry->d_name) || strlen(entry->d_name) > NAME_MAX) {
            continue;
        }
        if (track_count == capacity) {
            capacity *= 2;
            track_t *bigger = realloc(tracks, (size_t)capacity * sizeof(track_t));
            if (bigger == NULL) {
                closedir(dir);
                track_list_cleanup();
                return false;
            }
            tracks = bigger;
        }
        track_t *track = &tracks[track_count];
        memset(track, 0, sizeof(*track));
        snprintf(track->file_name, sizeof(track->file_name), "%s", entry->d_name);
        make_display_name(entry->d_name, track->display_name, sizeof(track->display_name));
        track_count++;
    }
    closedir(dir);

    qsort(tracks, (size_t)track_count, sizeof(track_t), compare_tracks);

    for (int i = 0; i < track_count; i++) {
        size_t path_size = strlen(directory) + strlen(tracks[i].file_name) + 2;
        tracks[i].path = malloc(path_size);
        if (tracks[i].path == NULL) {
            track_list_cleanup();
            return false;
        }
        snprintf(tracks[i].path, path_size, "%s/%s", directory, tracks[i].file_name);
    }
    return true;
}

int track_list_count(void)
{
    return track_count;
}

const char *track_list_name(int index)
{
    if (index < 0 || index >= track_count) {
        return NULL;
    }
    return tracks[index].display_name;
}

const char *track_list_path(int index)
{
    if (index < 0 || index >= track_count) {
        return NULL;
    }
    return tracks[index].path;
}

void track_list_cleanup(void)
{
    for (int i = 0; i < track_count; i++) {
        free(tracks[i].path);
    }
    free(tracks);
    tracks = NULL;
    track_count = 0;
}
