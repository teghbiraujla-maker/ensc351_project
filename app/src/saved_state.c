#include "saved_state.h"
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void saved_state_clear(saved_state_t *state)
{
    state->track[0] = '\0';
    state->position_sec = 0.0;
    state->volume_percent = -1;
    state->highlighted[0] = '\0';
}

bool saved_state_load(const char *path, saved_state_t *state)
{
    saved_state_clear(state);
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return false;
    }
    char line[SAVED_STATE_NAME_CHARS + 32];
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        char *equals = strchr(line, '=');
        if (line[0] == '#' || equals == NULL) {
            continue;
        }
        *equals = '\0';
        const char *key = line;
        const char *value = equals + 1;
        if (!strcmp(key, "track")) {
            snprintf(state->track, sizeof(state->track), "%s", value);
        } else if (!strcmp(key, "position")) {
            state->position_sec = strtod(value, NULL);
            if (state->position_sec < 0.0) state->position_sec = 0.0;
        } else if (!strcmp(key, "volume")) {
            int volume = atoi(value);
            state->volume_percent = (volume >= 0 && volume <= 100) ? volume : -1;
        } else if (!strcmp(key, "highlighted")) {
            snprintf(state->highlighted, sizeof(state->highlighted), "%s", value);
        }
    }
    fclose(file);
    return true;
}

bool saved_state_save(const char *path, const saved_state_t *state)
{
    char temp_path[1100];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);

    FILE *file = fopen(temp_path, "w");
    if (file == NULL) {
        return false;
    }
    fprintf(file, "# SFU campus tour - saved automatically, safe to delete\n");
    fprintf(file, "track=%s\n", state->track);
    fprintf(file, "position=%.1f\n", state->position_sec);
    fprintf(file, "volume=%d\n", state->volume_percent);
    fprintf(file, "highlighted=%s\n", state->highlighted);
    bool ok = (fflush(file) == 0) && (fsync(fileno(file)) == 0);   // really on the SD card
    ok = (fclose(file) == 0) && ok;
    if (!ok || rename(temp_path, path) != 0) {
        unlink(temp_path);
        return false;
    }
    // Make the rename itself survive a power cut: flush the folder too.
    char folder[1100];
    snprintf(folder, sizeof(folder), "%s", path);
    int folder_fd = open(dirname(folder), O_RDONLY);
    if (folder_fd >= 0) {
        fsync(folder_fd);
        close(folder_fd);
    }
    return true;
}

bool saved_state_should_save(const saved_state_t *last_saved, const saved_state_t *now_state,
                             long long ms_since_last_save)
{
    if (strcmp(last_saved->track, now_state->track) != 0 ||
        strcmp(last_saved->highlighted, now_state->highlighted) != 0 ||
        last_saved->volume_percent != now_state->volume_percent) {
        return true;
    }
    double moved = now_state->position_sec - last_saved->position_sec;
    if (moved < 0) moved = -moved;
    return moved >= 1.0 && ms_since_last_save >= SAVED_STATE_INTERVAL_MS;
}
