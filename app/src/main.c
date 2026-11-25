// Campus tour player - audio playback with terminal controls.
//
// Type a track number to play it, then use the commands in "help" to pause,
// change the volume, and so on. The terminal is a stand-in for the keypad:
// all input arrives as input_event_t values from terminal_input.c.

#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "audio_player.h"
#include "hal/audio_device.h"
#include "hal/encoder.h" // Temporary encoder verification inclusion
#include "terminal_input.h"
#include "track_list.h"

#define POLL_INTERVAL_MS 200
#define VOLUME_STEP      10
#define SEEK_STEP_SEC    10

static volatile sig_atomic_t stop_requested = 0;
static int s_counter = 0; // Temp encoder

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static void install_signal_handlers(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;      // no SA_RESTART: lets poll() return on Ctrl-C
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGHUP, &action, NULL);       // e.g. the SSH session was closed
}

static void format_time(double seconds, char *out, size_t out_size)
{
    int total = (int)(seconds + 0.5);
    snprintf(out, out_size, "%02d:%02d", total / 60, total % 60);
}

static void print_track_list(void)
{
    int count = track_list_count();
    if (count == 0) {
        printf("No MP3 files found. Copy some into the audio folder.\n");
        return;
    }
    printf("Tour stops:\n");
    for (int i = 0; i < count; i++) {
        printf("  %2d  %s\n", i + 1, track_list_name(i));
    }
}

static void print_status(int current_track)
{
    audio_status_t status = audio_player_get_status();
    const char *state_text = "STOPPED";
    if (status.state == AUDIO_PLAYING) {
        state_text = "PLAYING";
    } else if (status.state == AUDIO_PAUSED) {
        state_text = "PAUSED";
    }

    if (status.state == AUDIO_STOPPED || current_track < 0) {
        printf("[%s]  volume %d%%\n", state_text, status.volume_percent);
        return;
    }
    char position[16];
    char duration[16];
    format_time(status.position_sec, position, sizeof(position));
    format_time(status.duration_sec, duration, sizeof(duration));
    printf("[%s]  %d. %s   %s / %s   volume %d%%\n", state_text, current_track + 1,
           track_list_name(current_track), position, duration, status.volume_percent);
}

static void default_audio_dir(char *out, size_t out_size)
{
    const char *from_env = getenv("TOUR_AUDIO_DIR");
    if (from_env != NULL && from_env[0] != '\0') {
        snprintf(out, out_size, "%s", from_env);
        return;
    }
    // Default: an "audio" folder next to the executable.
    char exe_path[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (length > 0) {
        exe_path[length] = '\0';
        char *last_slash = strrchr(exe_path, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';
            snprintf(out, out_size, "%s/audio", exe_path);
            return;
        }
    }
    snprintf(out, out_size, "audio");
}

static void print_usage(const char *program)
{
    printf("Usage: %s [--audio-dir DIR] [--device ALSA_DEVICE]\n", program);
    printf("  --audio-dir DIR     folder with the .mp3 files (default: audio/ next to the program,\n");
    printf("                      or $TOUR_AUDIO_DIR)\n");
    printf("  --device DEVICE     ALSA device, e.g. plughw:1,0 (default: auto-detect the USB adapter,\n");
    printf("                      or $TOUR_AUDIO_DEVICE)\n");
}

// Temp encoder verification function
static void onEncoderTurn(int direction) 
{
    s_counter += direction;
    printf("Encoder turned: Counter %d\n", s_counter);
} // End temp

int main(int argc, char *argv[])
{
    char audio_dir[PATH_MAX];
    default_audio_dir(audio_dir, sizeof(audio_dir));
    const char *requested_device = getenv("TOUR_AUDIO_DEVICE");

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--audio-dir") && i + 1 < argc) {
            snprintf(audio_dir, sizeof(audio_dir), "%s", argv[++i]);
        } else if (!strcmp(argv[i], "--device") && i + 1 < argc) {
            requested_device = argv[++i];
        } else {
            print_usage(argv[0]);
            return !strcmp(argv[i], "--help") ? 0 : 1;
        }
    }

    if (!track_list_load(audio_dir)) {
        fprintf(stderr, "Cannot read the audio folder '%s'.\n", audio_dir);
        return 1;
    }

    char device[64];
    bool device_found = audio_device_resolve(requested_device, device, sizeof(device));
    printf("Audio device: %s%s\n", device,
           device_found ? "" : "   (no USB audio adapter found - is it plugged in?)");

    if (!audio_player_init(device)) {
        track_list_cleanup();
        return 1;
    }
    install_signal_handlers();

    Encoder_init(onEncoderTurn); // Temp encoder initialization
    Encoder_setPressCallback(onEncoderPress);

    printf("\n=== SFU Burnaby Campus Tour ===\n");
    print_track_list();
    printf("\n%s", terminal_input_help_text());
    printf("\n> ");
    fflush(stdout);

    int current_track = -1;
    bool user_stopped = true;                 // true unless a track is expected to be playing
    audio_state_t previous_state = AUDIO_STOPPED;

    while (!stop_requested) {
        input_event_t event = terminal_input_poll(POLL_INTERVAL_MS);
        bool prompt_needed = (event.type != INPUT_NONE);

        switch (event.type) {
        case INPUT_SELECT_TRACK: {
            int index = event.value - 1;
            const char *path = track_list_path(index);
            if (path == NULL) {
                printf("There is no track %d (choose 1-%d).\n", event.value, track_list_count());
            } else if (audio_player_play(path)) {
                current_track = index;
                user_stopped = false;
                printf("Playing %d. %s\n", event.value, track_list_name(index));
            } else {
                char reason[200];
                audio_player_get_last_error(reason, sizeof(reason));
                printf("Could not play '%s': %s\n", track_list_name(index), reason);
            }
            break;
        }
        case INPUT_TOGGLE_PAUSE:
            if (!audio_player_toggle_pause()) {
                printf("Nothing is playing.\n");
            } else {
                audio_status_t status = audio_player_get_status();
                printf("%s\n", status.state == AUDIO_PAUSED ? "Paused." : "Resumed.");
            }
            break;
        case INPUT_STOP:
            user_stopped = true;
            audio_player_stop();
            printf("Stopped.\n");
            break;
        case INPUT_VOLUME_UP:
            audio_player_adjust_volume(VOLUME_STEP);
            printf("Volume %d%%\n", audio_player_get_status().volume_percent);
            break;
        case INPUT_VOLUME_DOWN:
            audio_player_adjust_volume(-VOLUME_STEP);
            printf("Volume %d%%\n", audio_player_get_status().volume_percent);
            break;
        case INPUT_SEEK_FORWARD:
        case INPUT_SEEK_BACK: {
            int seconds = (event.type == INPUT_SEEK_FORWARD) ? SEEK_STEP_SEC : -SEEK_STEP_SEC;
            if (audio_player_get_status().state == AUDIO_STOPPED) {
                printf("Nothing is playing.\n");
            } else {
                audio_player_seek_relative(seconds);
                printf("%s %d seconds.\n", seconds > 0 ? "Forward" : "Back", SEEK_STEP_SEC);
            }
            break;
        }
        case INPUT_LIST:
            print_track_list();
            break;
        case INPUT_STATUS:
            print_status(current_track);
            break;
        case INPUT_HELP:
            printf("%s", terminal_input_help_text());
            break;
        case INPUT_QUIT:
            stop_requested = 1;
            break;
        case INPUT_UNKNOWN:
            printf("Unknown command. Type h for help.\n");
            break;
        case INPUT_NONE:
            break;
        }

        // Tell the user when a recording ends by itself.
        audio_state_t state = audio_player_get_status().state;
        if (previous_state != AUDIO_STOPPED && state == AUDIO_STOPPED && !user_stopped) {
            char reason[200];
            audio_player_get_last_error(reason, sizeof(reason));
            if (reason[0] != '\0') {
                printf("\nPlayback problem: %s\n", reason);
            } else {
                printf("\nFinished: %s. Choose another stop.\n", track_list_name(current_track));
            }
            prompt_needed = true;
            user_stopped = true;
        }
        previous_state = state;

        if (prompt_needed && !stop_requested) {
            printf("> ");
            fflush(stdout);
        }
    }

    printf("\nGoodbye!\n");
    Encoder_cleanup(); // Added Encoder_cleanup (temporary)
    audio_player_cleanup();
    track_list_cleanup();
    return 0;
}
