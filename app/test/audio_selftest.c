// audio_selftest - checks the audio player end to end on this board.
//
//   ./audio_selftest [--device ALSA_DEVICE] [file.mp3]
//
// It plays a file (the first MP3 in the audio folder by default) and checks
// play, pause, resume, volume, stop and error handling, printing PASS/FAIL for
// each. Listen while it runs: you should hear about 5 seconds of audio with a
// short silence in the middle (the pause).

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "audio_player.h"
#include "hal/audio_device.h"
#include "track_list.h"

static int failures = 0;

static void sleep_ms(int milliseconds)
{
    struct timespec delay = { milliseconds / 1000, (long)(milliseconds % 1000) * 1000000L };
    nanosleep(&delay, NULL);
}

static void check(bool condition, const char *description)
{
    printf("  [%s] %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) {
        failures++;
    }
}

// Wait up to timeout_ms for the player to report `wanted`.
static bool wait_for_state(audio_state_t wanted, int timeout_ms)
{
    for (int waited = 0; waited <= timeout_ms; waited += 50) {
        if (audio_player_get_status().state == wanted) {
            return true;
        }
        sleep_ms(50);
    }
    return false;
}

static bool wait_for_position(double seconds, int timeout_ms)
{
    for (int waited = 0; waited <= timeout_ms; waited += 50) {
        if (audio_player_get_status().position_sec >= seconds) {
            return true;
        }
        sleep_ms(50);
    }
    return false;
}

int main(int argc, char *argv[])
{
    const char *requested_device = NULL;
    const char *file = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--device") && i + 1 < argc) {
            requested_device = argv[++i];
        } else {
            file = argv[i];
        }
    }

    char audio_dir[PATH_MAX + 8] = "audio";
    char exe_path[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (length > 0) {
        exe_path[length] = '\0';
        char *slash = strrchr(exe_path, '/');
        if (slash != NULL) {
            *slash = '\0';
            snprintf(audio_dir, sizeof(audio_dir), "%s/audio", exe_path);
        }
    }
    if (file == NULL) {
        if (!track_list_load(audio_dir) || track_list_count() == 0) {
            fprintf(stderr, "No MP3 files in '%s'. Pass a file name instead.\n", audio_dir);
            return 1;
        }
        file = track_list_path(0);
    }

    char device[64];
    audio_device_resolve(requested_device, device, sizeof(device));
    printf("Audio self-test\n  device: %s\n  file:   %s\n\n", device, file);

    bool started = audio_player_init(device);
    check(started, "player starts (mpg123 installed)");
    if (!started) {
        track_list_cleanup();
        return 1;
    }

    check(audio_player_play(file), "play command accepted");
    check(wait_for_state(AUDIO_PLAYING, 3000), "state becomes PLAYING");
    check(wait_for_position(1.0, 5000), "position advances (audio is actually decoding)");
    check(audio_player_get_status().duration_sec > 1.0, "duration is reported");

    check(audio_player_toggle_pause(), "pause command accepted");
    check(wait_for_state(AUDIO_PAUSED, 2000), "state becomes PAUSED");
    sleep_ms(300);
    double paused_at = audio_player_get_status().position_sec;
    sleep_ms(1000);
    double still_at = audio_player_get_status().position_sec;
    check(still_at - paused_at < 0.1, "position holds still while paused");

    check(audio_player_toggle_pause(), "resume command accepted");
    check(wait_for_state(AUDIO_PLAYING, 2000), "state becomes PLAYING again");
    check(wait_for_position(still_at + 0.5, 5000), "position advances after resume");

    audio_player_set_volume(40);
    sleep_ms(300);
    check(audio_player_get_status().volume_percent == 40, "volume set to 40%");
    audio_player_adjust_volume(-100);
    check(audio_player_get_status().volume_percent == 0, "volume clamps at 0%");
    audio_player_set_volume(70);

    audio_player_stop();
    check(wait_for_state(AUDIO_STOPPED, 2000), "state becomes STOPPED after stop");

    check(!audio_player_play("/no/such/file.mp3"), "missing file is rejected");
    check(audio_player_get_status().state == AUDIO_STOPPED, "still STOPPED after a bad request");

    audio_player_cleanup();
    track_list_cleanup();
    printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
           failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
