// Host-side test of the tour's control rules (tour_controller) against a
// fake audio player - no sound card, mpg123, encoder, joystick or screen.
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

static bool fake_load_paused(const char *path, double position)
{
    if (!fake_play(path)) return false;
    fake.state = AUDIO_PAUSED;
    fake.position_sec = position;
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
    fake_get_status, fake_play, fake_load_paused, fake_toggle_pause, fake_stop,
    fake_adjust_volume, fake_seek, fake_last_error,
};

static void finish_track(void) { fake.state = AUDIO_STOPPED; }   // ends by itself

static void send(control_type_t type, int steps)
{
    control_event_t e = { type, steps };
    tour_controller_handle(e);
}
static void turn(int steps)  { send(CONTROL_TURN, steps); }
static void press(void)      { send(CONTROL_PRESS, 0); }
static void hold(void)       { send(CONTROL_LONG_PRESS, 0); }
static void volume(int s)    { send(CONTROL_VOLUME, s); }
static void seek(int s)      { send(CONTROL_SEEK_STEP, s); }
static void release(void)    { send(CONTROL_SEEK_RELEASE, 0); }
static void guide(void)      { send(CONTROL_GUIDE, 0); }

static bool played(const char *name) { return strstr(last_played, name) != NULL; }

// ---------------------------------------------------------------------------
static char audio_dir[] = "/tmp/tour_controller_test_XXXXXX";
static const char *names[] = { "01_Welcome.mp3", "02_Library.mp3", "03_Academic_Quadrangle.mp3" };

static void make_tracks(void)
{
    if (mkdtemp(audio_dir) == NULL) {
        perror("mkdtemp");
        exit(2);
    }
    for (int i = 0; i < 3; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", audio_dir, names[i]);
        FILE *f = fopen(path, "w");
        if (f) fclose(f);
    }
}

static void remove_tracks(void)
{
    for (int i = 0; i < 3; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", audio_dir, names[i]);
        unlink(path);
    }
    rmdir(audio_dir);
}

int main(void)
{
    app_log_init(NULL, false);
    make_tracks();
    if (!track_list_load(audio_dir) || track_list_count() != 3) {
        printf("could not set up test tracks\n");
        return 2;
    }
    tour_controller_init(&fake_player);
    tour_view_t v;

    printf("Encoder: choosing and playing\n");
    v = tour_controller_view();
    CHECK(v.mode == TOUR_MODE_CHOOSING && v.highlighted == 0 && v.playing == -1, "starts choosing, stop 1 highlighted");
    turn(-1);
    CHECK(tour_controller_view().highlighted == 0, "turning left at stop 1 stays there (no wrap)");
    turn(+5);
    CHECK(tour_controller_view().highlighted == 2, "turning far right stops at the last stop");
    turn(-1);
    CHECK(tour_controller_view().highlighted == 1 && play_calls == 0, "turning only moves the highlight");
    hold();
    CHECK(stop_calls == 0, "hold with nothing loaded does nothing");
    press();
    v = tour_controller_view();
    CHECK(played("02_Library") && v.playing == 1 && v.mode == TOUR_MODE_PLAYING, "press plays the highlighted stop");

    printf("Encoder: during playback\n");
    turn(+1);
    v = tour_controller_view();
    CHECK(v.highlighted == 2 && v.playing == 1 && play_calls == 1, "turning while playing moves the highlight, keeps playing");
    CHECK(fake.volume_percent == 70, "turning no longer changes the volume");
    press();
    CHECK(tour_controller_view().mode == TOUR_MODE_PAUSED && play_calls == 1, "press pauses (does not switch to the highlighted stop)");
    press();
    CHECK(tour_controller_view().mode == TOUR_MODE_PLAYING, "press resumes");
    hold();
    v = tour_controller_view();
    CHECK(stop_calls == 1 && v.mode == TOUR_MODE_CHOOSING && v.playing == -1, "hold ends the track");
    CHECK(v.popup == TOUR_POPUP_NONE && v.highlighted == 2, "...with no popup, and the highlight left alone");
    tour_controller_tick();
    CHECK(tour_controller_view().popup == TOUR_POPUP_NONE, "an ended track is not treated as 'finished'");
    press();
    CHECK(played("03_Academic") && tour_controller_view().playing == 2, "then press plays the newly highlighted stop");

    printf("Joystick: volume\n");
    volume(+1);
    CHECK(fake.volume_percent == 75, "up: +5%");
    volume(-1); volume(-1);
    CHECK(fake.volume_percent == 65, "down twice: -10%");

    printf("Joystick: skipping (preview, then one jump on release)\n");
    fake.position_sec = 10.4;                               // tracks are 60 s long
    int seeks_before = seek_calls;
    for (int i = 0; i < 20; i++) seek(+1);                  // held right for 2 s
    v = tour_controller_view();
    CHECK(seek_calls == seeks_before, "while held: no jumps sent to the player");
    CHECK(v.scrubbing && v.position_sec > 30.3 && v.position_sec < 30.5, "while held: the bar previews +20 s");
    CHECK(v.audio.position_sec > 10.3 && v.audio.position_sec < 10.5, "while held: the audio stays where it was");
    release();
    v = tour_controller_view();
    CHECK(seek_calls == seeks_before + 1 && last_seek == 20 && !v.scrubbing, "on release: exactly one jump of +20 s");
    for (int i = 0; i < 100; i++) seek(+1);
    CHECK(tour_controller_view().position_sec <= 59.0, "the preview never goes past end - 1 s");
    release();
    CHECK(fake.position_sec <= 59.0 && fake.state != AUDIO_STOPPED, "...so skipping never ends the track");
    for (int i = 0; i < 100; i++) seek(-1);
    release();
    CHECK(fake.position_sec == 0.0, "skipping far back stops at 0:00");
    press();                                                // pause
    for (int i = 0; i < 5; i++) seek(+1);
    release();
    CHECK(fake.position_sec == 5.0 && tour_controller_view().mode == TOUR_MODE_PAUSED, "skipping works while paused");
    press();                                                // resume
    hold();
    seeks_before = seek_calls;
    seek(+1);
    release();
    CHECK(seek_calls == seeks_before && !tour_controller_view().scrubbing, "skipping with nothing loaded does nothing");

    printf("Directions popup\n");
    turn(-5);                                               // stop 1
    guide();
    CHECK(tour_controller_view().popup == TOUR_POPUP_NONE, "stop 1 has no directions");
    turn(+1);
    guide();
    v = tour_controller_view();
    CHECK(v.popup == TOUR_POPUP_GUIDE && v.popup_stop == 1, "joystick press shows directions for the highlighted stop");
    turn(+1);
    CHECK(tour_controller_view().highlighted == 1, "with the popup open, turning is ignored");
    int plays_before = play_calls;
    press();
    CHECK(play_calls == plays_before, "with the popup open and nothing loaded, press is ignored");
    guide();
    CHECK(tour_controller_view().popup == TOUR_POPUP_NONE, "joystick press again closes it");

    printf("Track finishing by itself\n");
    press();                                                // play stop 2
    guide();                                                // open the popup while playing
    press();
    CHECK(tour_controller_view().mode == TOUR_MODE_PAUSED, "behind the popup, press still pauses");
    volume(+1);
    CHECK(fake.volume_percent == 65, "behind the popup, volume is ignored");
    press();
    guide();                                                // close
    finish_track();
    tour_controller_tick();
    v = tour_controller_view();
    CHECK(v.highlighted == 2 && v.popup == TOUR_POPUP_GUIDE && v.popup_stop == 2,
          "stop 2 ends: stop 3 highlighted and its directions pop up");
    CHECK(fake.state == AUDIO_STOPPED, "nothing starts on its own");
    guide();
    press();
    CHECK(played("03_Academic"), "close, press: the next stop plays");
    finish_track();
    tour_controller_tick();
    CHECK(tour_controller_view().popup == TOUR_POPUP_TOUR_COMPLETE, "the last stop ends: 'tour complete' popup");
    guide();
    CHECK(tour_controller_view().popup == TOUR_POPUP_NONE, "joystick press closes it");

    printf("Out-of-order choice: no automatic directions\n");
    press();                                                // stop 3 playing
    turn(-2);                                               // highlight stop 1
    hold();
    press();                                                // play stop 1 out of order
    v = tour_controller_view();
    CHECK(v.playing == 0 && v.popup == TOUR_POPUP_NONE, "jumping to another stop shows no directions");

    printf("Errors and resume\n");
    snprintf(fake_error, sizeof(fake_error), "Device or resource busy");
    finish_track();
    tour_controller_tick();
    v = tour_controller_view();
    CHECK(v.mode == TOUR_MODE_CHOOSING && v.popup == TOUR_POPUP_NONE, "playback error: no popup");
    fake_play_fails = true;
    press();
    CHECK(tour_controller_view().mode == TOUR_MODE_CHOOSING, "a file that cannot play leaves us choosing");
    fake_play_fails = false;
    CHECK(tour_controller_resume(1, 42.0), "resume loads a stop");
    v = tour_controller_view();
    CHECK(v.mode == TOUR_MODE_PAUSED && v.playing == 1 && v.highlighted == 1 && fake.position_sec == 42.0,
          "resume: paused at the saved spot, highlighted");

    track_list_cleanup();
    remove_tracks();
    app_log_cleanup();
    printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
           failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
