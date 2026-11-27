// progress_store.c
#include "progress_store.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HEADER_LINE   "TOUR_PROGRESS_V1"
#define END_LINE      "END"
#define MAX_POSITION  86400.0       // 24 hours: anything larger is treated as corrupt
#define PATH_BUF      1024

void progress_store_default_path(char *out, size_t out_size)
{
    const char *from_env = getenv("TOUR_PROGRESS_FILE");
    if (from_env != NULL && from_env[0] != '\0') {
        snprintf(out, out_size, "%s", from_env);
        return;
    }
    const char *home = getenv("HOME");
    if (home != NULL && home[0] != '\0') {
        snprintf(out, out_size, "%s/.sfu_tour_progress", home);
        return;
    }
    snprintf(out, out_size, "sfu_tour_progress.txt");
}

static bool valid_file_name(const char *name)
{
    size_t length = (name == NULL) ? 0 : strlen(name);
    if (length == 0 || length >= PROGRESS_TRACK_FILE_MAX) {
        return false;
    }
    return strchr(name, '/') == NULL && strchr(name, '\n') == NULL
        && strchr(name, '\r') == NULL;
}

static bool valid_position(double position_sec)
{
    return isfinite(position_sec) && position_sec >= 0.0 && position_sec <= MAX_POSITION;
}

bool progress_store_save(const char *path, const char *track_file, double position_sec)
{
    if (path == NULL || !valid_file_name(track_file) || !valid_position(position_sec)) {
        return false;
    }
    char temp_path[PATH_BUF];
    if (snprintf(temp_path, sizeof(temp_path), "%s.tmp", path) >= (int)sizeof(temp_path)) {
        return false;
    }

    FILE *file = fopen(temp_path, "w");
    if (file == NULL) {
        return false;
    }
    bool ok = fprintf(file, "%s\n%.1f\n%s\n%s\n", HEADER_LINE, position_sec,
                      track_file, END_LINE) > 0;
    ok = ok && (fflush(file) == 0) && (fsync(fileno(file)) == 0);
    ok = (fclose(file) == 0) && ok;
    // Only replace the old file once the new one is completely written.
    if (!ok || rename(temp_path, path) != 0) {
        remove(temp_path);
        return false;
    }
    return true;
}

// Read one line without its trailing newline. False at end of file or if too long.
static bool read_line(FILE *file, char *out, size_t out_size)
{
    if (fgets(out, (int)out_size, file) == NULL) {
        return false;
    }
    size_t length = strlen(out);
    if (length == 0 || out[length - 1] != '\n') {
        return false;                       // no newline: truncated or too long
    }
    out[length - 1] = '\0';
    return true;
}

bool progress_store_load(const char *path, saved_progress_t *out)
{
    if (path == NULL || out == NULL) {
        return false;
    }
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }

    char header[64], position_text[64], name[PROGRESS_TRACK_FILE_MAX + 2], end[16];
    bool ok = read_line(file, header, sizeof(header)) && strcmp(header, HEADER_LINE) == 0
           && read_line(file, position_text, sizeof(position_text))
           && read_line(file, name, sizeof(name)) && valid_file_name(name)
           && read_line(file, end, sizeof(end)) && strcmp(end, END_LINE) == 0;
    fclose(file);
    if (!ok) {
        return false;
    }

    char *parse_end = NULL;
    double position = strtod(position_text, &parse_end);
    if (parse_end == position_text || *parse_end != '\0' || !valid_position(position)) {
        return false;
    }
    size_t name_length = strlen(name);          // already checked: < PROGRESS_TRACK_FILE_MAX
    memcpy(out->track_file, name, name_length);
    out->track_file[name_length] = '\0';
    out->position_sec = position;
    return true;
}

void progress_store_clear(const char *path)
{
    if (path != NULL) {
        remove(path);
    }
}
