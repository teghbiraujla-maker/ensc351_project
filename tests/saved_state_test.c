// Host-side test of the save / resume file.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "saved_state.h"

static int failures = 0;

#define CHECK(condition, description) do { \
    int ok_ = (condition); \
    printf("  [%s] %s\n", ok_ ? "PASS" : "FAIL", description); \
    if (!ok_) { printf("         (line %d: %s)\n", __LINE__, #condition); failures++; } } while (0)

int main(void)
{
    char folder[] = "/tmp/saved_state_test_XXXXXX";
    if (mkdtemp(folder) == NULL) return 2;
    char path[256], temp_path[300];
    snprintf(path, sizeof(path), "%s/tour_state.txt", folder);
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);

    saved_state_t s, loaded;
    printf("Saving and loading\n");
    CHECK(!saved_state_load(path, &loaded) && loaded.volume_percent == -1 && loaded.track[0] == '\0',
          "no file yet: load fails, state cleared");
    saved_state_clear(&s);
    snprintf(s.track, sizeof(s.track), "05_AQ.mp3");
    s.position_sec = 123.44;
    s.volume_percent = 55;
    snprintf(s.highlighted, sizeof(s.highlighted), "06_ASB.mp3");
    CHECK(saved_state_save(path, &s), "save");
    CHECK(access(temp_path, F_OK) != 0, "no leftover .tmp file");
    CHECK(saved_state_load(path, &loaded), "load");
    CHECK(!strcmp(loaded.track, "05_AQ.mp3") && !strcmp(loaded.highlighted, "06_ASB.mp3"), "track names survive");
    CHECK(loaded.position_sec > 123.3 && loaded.position_sec < 123.5 && loaded.volume_percent == 55,
          "position (0.1 s) and volume survive");

    FILE *f = fopen(path, "w");
    fputs("garbage\nvolume=250\nposition=-4\n", f);
    fclose(f);
    CHECK(saved_state_load(path, &loaded) && loaded.volume_percent == -1 && loaded.position_sec == 0.0,
          "nonsense values are ignored");

    printf("When to save\n");
    saved_state_t a = s, b = s;
    CHECK(!saved_state_should_save(&a, &b, 100000), "nothing changed: no save");
    b.position_sec += 0.5;
    CHECK(!saved_state_should_save(&a, &b, 100000), "moved under 1 s: no save");
    b.position_sec = s.position_sec + 3.0;
    CHECK(!saved_state_should_save(&a, &b, 2000), "moved 3 s but saved 2 s ago: wait");
    CHECK(saved_state_should_save(&a, &b, SAVED_STATE_INTERVAL_MS), "moved 3 s, 5 s since last save: save");
    b = s;
    b.volume_percent = 60;
    CHECK(saved_state_should_save(&a, &b, 0), "volume changed: save at once");
    b = s;
    snprintf(b.track, sizeof(b.track), "06_ASB.mp3");
    CHECK(saved_state_should_save(&a, &b, 0), "different stop: save at once");
    b = s;
    b.highlighted[0] = '\0';
    CHECK(saved_state_should_save(&a, &b, 0), "highlight changed: save at once");

    unlink(path);
    rmdir(folder);
    printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
           failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
