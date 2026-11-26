// Host-side test of the encoder control scheme (tour_controller) against a
// fake audio player - no sound card, no mpg123, no encoder needed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "app_log.h"
#include "tour_controller.h"
#include "track_list.h"

static int failures = 0;

#define CHECK(condition, description) do { \
    int ok_ = (condition); \
    printf("  [%s] %s\n", ok_ ? "PASS" : "FAIL", description); \
    if (!ok_) { printf("         (line %d: %s)\n", __LINE__, #condition); failures++; } } while (0)

// ---------------------------------------------------------------------------
// Fake player: behaves like audio_player.c (state changes immediately).
// ---------------------------------------------------------------------------
static audio_status_t fake = { AUDIO_STOPPED, 0.0, 0.0, 70 };
static char fake_error[200] = "";
static char last_played[512] = "";
static bool fake_play_fails = false;
static int play_calls = 0, stop_calls = 0, seek_calls = 0, last_seek = 0;

static audio_status_t fake_get_status(void) { return fake; }

static bool fake_play(const char *path)
{
    play_calls++;
    if (fake_play_fails) {
        snprintf(fake_error, sizeof(fake_error), "file is broken");
        return false;
    }
    snprintf(last_played, sizeof(last_played), "%s", path);
    fake_error[0] = '\0';
    fake.state = AUDIO_PLAYING;
    fake.position_sec = 0.0;
    fake.duration_sec = 60.0;
    return true;
}

static bool fake_toggle_pause(void)
{
    if (fake.state == AUDIO_STOPPED) return false;
    fake.state = (fake.state == AUDIO_PLAYING) ? AUDIO_PAUSED : AUDIO_PLAYING;
    return true;
}

static void fake_stop(void) { stop_calls++; fake.state = AUDIO_STOPPED; }

static void fake_adjust_volume(int delta)
{
    fake.volume_percent += delta;
    if (fake.volume_percent < 0) fake.volume_percent = 0;
    if (fake.volume_percent > 100) fake.volume_percent = 100;
}

static void fake_seek(int seconds)
{
    seek_calls++;
    last_seek = seconds;
    fake.position_sec += seconds;
    if (fake.position_sec < 0) fake.position_sec = 0;
    if (fake.position_sec > fake.duration_sec) fake.position_sec = fake.duration_sec;
}

static void fake_last_error(char *out, size_t size) { snprintf(out, size, "%s", fake_error); }

static const tour_player_t fake_player = {
    fake_get_status, fake_play, fake_toggle_pause, fake_stop,
    fake_adjust_volume, fake_seek, fake_last_error,
};

// The track finishes by itself (mpg123 reports "stopped").
static void finish_track(void) { fake.state = AUDIO_STOPPED; }

static void turn(int steps) { control_event_t e = { CONTROL_TURN, steps }; tour_controller_handle(e); }
static void press(void)     { control_event_t e = { CONTROL_PRESS, 0 };    tour_controller_handle(e); }
static void hold(void)      { control_event_t e = { CONTROL_LONG_PRESS, 0 }; tour_controller_handle(e); }

static bool played(const char *name) { return strstr(last_played, name) != NULL; }

// ---------------------------------------------------------------------------
static char audio_dir[] = "/tmp/tour_controller_test_XXXXXX";

static void make_tracks(void)
{
    if (mkdtemp(audio_dir) == NULL) {
        perror("mkdtemp");
        exit(2);
    }
    const char *names[] = { "01_Welcome.mp3", "02_Library.mp3", "03_Academic_Quadrangle.mp3" };
    for (int i = 0; i < 3; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", audio_dir, names[i]);
        FILE *f = fopen(path, "w");
        if (f) fclose(f);
    }
}

static void remove_tracks(void)
{
    const char *names[] = { "01_Welcome.mp3", "02_Library.mp3", "03_Academic_Quadrangle.mp3" };
    for (int i = 0; i < 3; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", audio_dir, names[i]);
        unlink(path);
    }
    rmdir(audio_dir);
}

int main(void)
{
    app_log_init(NULL, false);   // keep the test output readable
    make_tracks();
    if (!track_list_load(audio_dir) || track_list_count() != 3) {
        printf("could not set up test tracks\n");
        return 2;
    }
    tour_controller_init(&fake_player);
    tour_view_t v;

    printf("Choosing a stop\n");
    v = tour_controller_view();
    CHECK(v.mode == TOUR_MODE_CHOOSING && v.focus == TOUR_FOCUS_TRACK_LIST, "starts in CHOOSING, focus on the list");
    CHECK(v.highlighted == 0 && v.current == -1 && v.track_count == 3, "first stop highlighted, nothing played yet");
    turn(-1);
    CHECK(tour_controller_view().highlighted == 0, "turning left at the first stop stays there (no wrap)");
    turn(+5);
    CHECK(tour_controller_view().highlighted == 2, "turning far right stops at the last stop");
    turn(-1);
    CHECK(tour_controller_view().highlighted == 1, "one click left: stop 2");
    hold();
    CHECK(stop_calls == 0, "hold while choosing does nothing");

    printf("Playing\n");
    press();
    v = tour_controller_view();
    CHECK(played("02_Library") && v.current == 1, "press plays the highlighted stop");
    CHECK(v.mode == TOUR_MODE_PLAYING && v.focus == TOUR_FOCUS_VOLUME, "PLAYING, focus on volume");
    turn(+2);
    CHECK(fake.volume_percent == 80, "two clicks right: volume +10% (5% per click)");
    turn(-1);
    CHECK(fake.volume_percent == 75, "one click left: volume -5%");
    CHECK(seek_calls == 0, "turning while playing does not skip");

    printf("Paused\n");
    press();
    v = tour_controller_view();
    CHECK(v.mode == TOUR_MODE_PAUSED && v.focus == TOUR_FOCUS_PROGRESS, "press pauses; focus on the progress bar");
    fake.position_sec = 10.4;                              // track is 60 s long
    turn(+1);
    CHECK(last_seek == 5 && fake.position_sec > 15.3 && fake.position_sec < 15.5, "one click right: skip +5 s");
    CHECK(fake.volume_percent == 75, "turning while paused does not change the volume");
    turn(+20);
    CHECK(fake.position_sec <= 59.0 && fake.position_sec > 58.0, "skipping far forward stops before end - 1 s");
    int seeks_before = seek_calls;
    turn(+1);
    CHECK(seek_calls == seeks_before, "at the end guard, more clicks right do nothing");
    CHECK(tour_controller_view().mode == TOUR_MODE_PAUSED, "...and the track has not ended");
    turn(-20);
    CHECK(fake.position_sec == 0.0, "skipping far back stops at 0:00");
    press();
    CHECK(tour_controller_view().mode == TOUR_MODE_PLAYING, "press resumes");

    printf("Hold to stop\n");
    hold();
    v = tour_controller_view();
    CHECK(stop_calls == 1 && v.mode == TOUR_MODE_CHOOSING, "hold stops and goes back to the list");
    CHECK(v.highlighted == 1, "the stopped stop stays highlighted");
    tour_controller_tick();
    CHECK(tour_controller_view().highlighted == 1, "a stop on purpose is not treated as 'finished'");

    printf("Track finishing by itself\n");
    press();
    finish_track();
    tour_controller_tick();
    v = tour_controller_view();
    CHECK(v.mode == TOUR_MODE_CHOOSING && v.highlighted == 2, "after stop 2 finishes, stop 3 is highlighted");
    CHECK(fake.state == AUDIO_STOPPED && play_calls == 2, "nothing starts on its own");
    press();
    CHECK(played("03_Academic_Quadrangle"), "one press plays the next stop");
    finish_track();
    tour_controller_tick();
    v = tour_controller_view();
    CHECK(v.tour_complete && v.highlighted == 2, "the last stop finishing marks the tour complete");
    turn(-1);
    CHECK(!tour_controller_view().tour_complete, "turning clears 'tour complete'");

    printf("Errors\n");
    press();                                              // plays stop 2
    snprintf(fake_error, sizeof(fake_error), "Device or resource busy");
    finish_track();
    tour_controller_tick();
    v = tour_controller_view();
    CHECK(v.mode == TOUR_MODE_CHOOSING && v.highlighted == 1, "playback error: back to the list on the same stop");
    fake_play_fails = true;
    int current_before = tour_controller_view().current;
    press();
    v = tour_controller_view();
    CHECK(v.mode == TOUR_MODE_CHOOSING && v.current == current_before, "a file that cannot play leaves us choosing");
    fake_play_fails = false;

    track_list_cleanup();
    remove_tracks();
    app_log_cleanup();
    printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
           failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
