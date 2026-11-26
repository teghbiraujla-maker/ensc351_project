// Campus tour player - audio tour controlled with one rotary encoder.
//
// Inputs: the encoder (hal/encoder.c) and, for debugging, typed commands
// (terminal_input.c) both produce control_event_t values, which go through
// one queue (input_queue.c) to the main loop. tour_controller.c decides what
// each event does. All messages go through app_log.c.
//
// Main loop, every FRAME_INTERVAL_MS (or sooner when an input arrives):
//   1. handle waiting inputs
//   2. tour_controller_tick()  - notices a track ending by itself
//   3. (GUI, later) draw tour_controller_view()

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "app_log.h"
#include "audio_player.h"
#include "controls.h"
#include "hal/audio_device.h"
#include "hal/encoder.h"
#include "input_queue.h"
#include "playback_display.h"
#include "terminal_input.h"
#include "tour_controller.h"
#include "track_list.h"

#define FRAME_INTERVAL_MS 100    // how often the loop runs when nothing happens (GUI redraw rate)

static volatile sig_atomic_t stop_requested = 0;

// The real player, as seen by tour_controller (the unit test uses a fake one).
static const tour_player_t real_player = {
    .get_status     = audio_player_get_status,
    .play           = audio_player_play,
    .toggle_pause   = audio_player_toggle_pause,
    .stop           = audio_player_stop,
    .adjust_volume  = audio_player_adjust_volume,
    .seek_relative  = audio_player_seek_relative,
    .get_last_error = audio_player_get_last_error,
};

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

// ---------------------------------------------------------------------------
// Inputs -> queue. The encoder callbacks run on the encoder's thread, so they
// only push into the thread-safe queue; the main loop does the rest.
// ---------------------------------------------------------------------------
static void queue_event(control_event_t event)
{
    if (!input_queue_push(event)) {
        app_log("Input queue full - input dropped.");
    }
}

static void on_encoder_turn(int direction)
{
    control_event_t event = { CONTROL_TURN, direction };
    queue_event(event);
}

static void on_encoder_press(Encoder_press_t press)
{
    control_event_t event = { press == ENCODER_PRESS_LONG ? CONTROL_LONG_PRESS : CONTROL_PRESS, 0 };
    queue_event(event);
}

// ---------------------------------------------------------------------------
// Debug commands
// ---------------------------------------------------------------------------
static void log_track_list(void)
{
    tour_view_t view = tour_controller_view();
    if (view.track_count == 0) {
        app_log("No MP3 files found. Copy some into the audio folder.");
        return;
    }
    app_log("Tour stops (> highlighted, * playing):");
    for (int i = 0; i < view.track_count; i++) {
        bool is_current = (i == view.current && view.mode != TOUR_MODE_CHOOSING);
        app_log("  %c%c %2d  %s", i == view.highlighted ? '>' : ' ', is_current ? '*' : ' ',
                i + 1, track_list_name(i));
    }
}

static void log_status(void)
{
    static const char *focus_names[] = {
        [TOUR_FOCUS_TRACK_LIST] = "choose a stop",
        [TOUR_FOCUS_VOLUME]     = "volume",
        [TOUR_FOCUS_PROGRESS]   = "skip",
    };
    tour_view_t view = tour_controller_view();
    if (view.mode == TOUR_MODE_CHOOSING) {
        if (view.highlighted >= 0) {
            app_log("[CHOOSING] highlighted %d/%d: %s   volume %d%%   (turn = %s)",
                    view.highlighted + 1, view.track_count, track_list_name(view.highlighted),
                    view.audio.volume_percent, focus_names[view.focus]);
        } else {
            app_log("[CHOOSING] no tracks   volume %d%%", view.audio.volume_percent);
        }
        return;
    }
    char time_text[32];
    playback_display_time(view.audio.position_sec, view.audio.duration_sec,
                          time_text, sizeof(time_text));
    app_log("[%s] %d. %s   %s (%.0f%%)   volume %d%%   (turn = %s)",
            tour_controller_mode_name(view.mode), view.current + 1, track_list_name(view.current),
            time_text, 100.0 * playback_display_progress(&view.audio),
            view.audio.volume_percent, focus_names[view.focus]);
}

static void handle_event(control_event_t event)
{
    switch (event.type) {
    case CONTROL_TURN:
    case CONTROL_PRESS:
    case CONTROL_LONG_PRESS:
        tour_controller_handle(event);
        break;
    case CONTROL_STATUS:
        log_status();
        break;
    case CONTROL_LIST:
        log_track_list();
        break;
    case CONTROL_HELP:
        app_log("%s", terminal_input_help_text());
        break;
    case CONTROL_QUIT:
        stop_requested = 1;
        break;
    case CONTROL_UNKNOWN:
        app_log("Unknown command. Type h for help.");
        break;
    case CONTROL_NONE:
        break;
    }
}

// ---------------------------------------------------------------------------
// Start-up
// ---------------------------------------------------------------------------
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
    printf("Usage: %s [--audio-dir DIR] [--device ALSA_DEVICE] [--log FILE]\n", program);
    printf("  --audio-dir DIR     folder with the .mp3 files (default: audio/ next to the program,\n");
    printf("                      or $TOUR_AUDIO_DIR)\n");
    printf("  --device DEVICE     ALSA device, e.g. plughw:1,0 (default: auto-detect the USB adapter,\n");
    printf("                      or $TOUR_AUDIO_DEVICE)\n");
    printf("  --log FILE          also append every message to FILE (for debugging)\n");
}

int main(int argc, char *argv[])
{
    char audio_dir[PATH_MAX];
    default_audio_dir(audio_dir, sizeof(audio_dir));
    const char *requested_device = getenv("TOUR_AUDIO_DEVICE");
    const char *log_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--audio-dir") && i + 1 < argc) {
            snprintf(audio_dir, sizeof(audio_dir), "%s", argv[++i]);
        } else if (!strcmp(argv[i], "--device") && i + 1 < argc) {
            requested_device = argv[++i];
        } else if (!strcmp(argv[i], "--log") && i + 1 < argc) {
            log_path = argv[++i];
        } else {
            print_usage(argv[0]);
            return !strcmp(argv[i], "--help") ? 0 : 1;
        }
    }

    app_log_init(log_path, true);

    if (!track_list_load(audio_dir)) {
        app_log("Cannot read the audio folder '%s'.", audio_dir);
        app_log_cleanup();
        return 1;
    }

    char device[64];
    bool device_found = audio_device_resolve(requested_device, device, sizeof(device));
    app_log("Audio device: %s%s", device,
            device_found ? "" : "   (no USB audio adapter found - is it plugged in?)");

    if (!audio_player_init(device)) {
        track_list_cleanup();
        app_log_cleanup();
        return 1;
    }
    if (!input_queue_init()) {
        app_log("Cannot create the input queue.");
        audio_player_cleanup();
        track_list_cleanup();
        app_log_cleanup();
        return 1;
    }
    install_signal_handlers();

    // Without the encoder the program still runs: typed commands work the same way.
    if (!Encoder_init(on_encoder_turn, on_encoder_press)) {
        app_log("Encoder not available - use typed commands instead (h for help).");
    }

    tour_controller_init(&real_player);

    app_log("=== SFU Burnaby Campus Tour ===");
    log_track_list();
    app_log("Turn to choose a stop, press to play. Type h for the typed commands.");

    bool stdin_open = true;
    while (!stop_requested) {
        struct pollfd watch[2];
        int watch_count = 0;
        watch[watch_count++] = (struct pollfd){ input_queue_fd(), POLLIN, 0 };
        if (stdin_open) {
            watch[watch_count++] = (struct pollfd){ terminal_input_fd(), POLLIN, 0 };
        }

        int ready = poll(watch, (nfds_t)watch_count, FRAME_INTERVAL_MS);
        if (ready < 0 && errno != EINTR) {
            app_log("poll failed: %s", strerror(errno));
            break;
        }

        // Typed input: translated and queued like encoder input.
        if (ready > 0 && stdin_open && (watch[1].revents & (POLLIN | POLLHUP))) {
            if (!terminal_input_read(queue_event)) {
                stdin_open = false;
                app_log("Terminal input closed - the encoder still works.");
            }
        }

        control_event_t event;
        while (!stop_requested && input_queue_pop(&event)) {
            handle_event(event);
        }

        tour_controller_tick();

        // GUI: draw tour_controller_view() and the app_log lines here, once per frame.
    }

    app_log("Goodbye!");
    Encoder_cleanup();          // first: stops the thread that pushes into the queue
    audio_player_cleanup();
    input_queue_cleanup();
    track_list_cleanup();
    app_log_cleanup();
    return 0;
}
