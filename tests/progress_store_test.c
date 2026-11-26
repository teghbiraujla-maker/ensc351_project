// Hardware-free test for progress_store (uses a temporary folder).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "progress_store.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static void write_raw(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    if (file != NULL) { fputs(text, file); fclose(file); }
}

int main(void)
{
    char dir[] = "/tmp/progress_test_XXXXXX";
    if (mkdtemp(dir) == NULL) { printf("cannot create temp dir\n"); return 1; }
    char path[256];
    snprintf(path, sizeof(path), "%s/progress", dir);
    saved_progress_t saved;

    // No file yet -> nothing to resume (not an error)
    CHECK(!progress_store_load(path, &saved));

    // Round trip: recording 3 at 2:24 (144 s)
    CHECK(progress_store_save(path, "03_Convocation_Mall.mp3", 144.0));
    CHECK(progress_store_load(path, &saved));
    CHECK(strcmp(saved.track_file, "03_Convocation_Mall.mp3") == 0);
    CHECK(saved.position_sec == 144.0);

    // A newer save replaces the older one
    CHECK(progress_store_save(path, "04_DAC.mp3", 12.5));
    CHECK(progress_store_load(path, &saved));
    CHECK(strcmp(saved.track_file, "04_DAC.mp3") == 0 && saved.position_sec == 12.5);

    // Bad input is refused and does not destroy the good file
    CHECK(!progress_store_save(path, "", 5.0));
    CHECK(!progress_store_save(path, "a/b.mp3", 5.0));
    CHECK(!progress_store_save(path, NULL, 5.0));
    CHECK(!progress_store_save(path, "x.mp3", -1.0));
    CHECK(!progress_store_save(path, "x.mp3", 1e12));
    CHECK(progress_store_load(path, &saved) && strcmp(saved.track_file, "04_DAC.mp3") == 0);

    // Corrupted / partial files are rejected, never crash
    write_raw(path, "");                                         CHECK(!progress_store_load(path, &saved));
    write_raw(path, "garbage\n");                                CHECK(!progress_store_load(path, &saved));
    write_raw(path, "TOUR_PROGRESS_V1\n10.0\n04_DAC.mp3\n");     CHECK(!progress_store_load(path, &saved)); // no END (cut off)
    write_raw(path, "TOUR_PROGRESS_V1\n10.0\n04_DA");            CHECK(!progress_store_load(path, &saved)); // truncated
    write_raw(path, "TOUR_PROGRESS_V1\nabc\nx.mp3\nEND\n");      CHECK(!progress_store_load(path, &saved));
    write_raw(path, "TOUR_PROGRESS_V1\nnan\nx.mp3\nEND\n");      CHECK(!progress_store_load(path, &saved));
    write_raw(path, "TOUR_PROGRESS_V1\n-4\nx.mp3\nEND\n");       CHECK(!progress_store_load(path, &saved));
    write_raw(path, "TOUR_PROGRESS_V1\n5\n../etc/passwd\nEND\n");CHECK(!progress_store_load(path, &saved));
    write_raw(path, "OTHER_V9\n5\nx.mp3\nEND\n");                CHECK(!progress_store_load(path, &saved));

    // A leftover temp file from an interrupted save does not matter
    char temp_path[300];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    write_raw(temp_path, "half written");
    CHECK(progress_store_save(path, "05_AQ.mp3", 30.0));
    CHECK(progress_store_load(path, &saved) && strcmp(saved.track_file, "05_AQ.mp3") == 0);

    // Unwritable location: returns false, no crash
    CHECK(!progress_store_save("/nonexistent_dir_xyz/progress", "x.mp3", 1.0));

    // Clear
    progress_store_clear(path);
    CHECK(!progress_store_load(path, &saved));
    progress_store_clear(path);                  // clearing twice is harmless

    // Default path honours $TOUR_PROGRESS_FILE
    char default_path[300];
    setenv("TOUR_PROGRESS_FILE", "/tmp/custom_progress", 1);
    progress_store_default_path(default_path, sizeof(default_path));
    CHECK(strcmp(default_path, "/tmp/custom_progress") == 0);

    rmdir(dir);
    if (failures == 0) printf("progress_store: all tests passed\n");
    return failures ? 1 : 0;
}
