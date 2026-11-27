// Campus tour player - audio tour with a rotary encoder, a joystick and a
// 7" screen.
//
// Inputs: the encoder (hal/encoder.c), the joystick (joystick_input.c) and,
// for debugging, typed commands (terminal_input.c) all produce
// control_event_t values, which go through one queue (input_queue.c) to the
// main loop. tour_controller.c decides what each event does; gui.c draws the
// screen from tour_controller_view(); saved_state.c remembers where the tour
// was so it can resume after a power cut. All messages go through app_log.c.
//
// Main loop, every FRAME_INTERVAL_MS (or sooner when an input arrives):
//   1. handle waiting inputs        2. tour_controller_tick()
//   3. gui_render() (+ one background repair piece every 200 ms)
//   4. save the state if it changed

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "app_log.h"
#include "audio_player.h"
#include "canvas.h"
#include "controls.h"
#include "gui.h"
#include "hal/audio_device.h"
#include "hal/encoder.h"
#include "hal/ra8875.h"
#include "input_queue.h"
#include "joystick_input.h"
#include "playback_display.h"
#include "saved_state.h"
#include "terminal_input.h"
#include "tour_controller.h"
#include "track_list.h"

#define FRAME_INTERVAL_MS 100    // loop / screen update rate when nothing happens

static volatile sig_atomic_t stop_requested = 0;

// The real player, as seen by tour_controller (the unit test uses a fake one).
static const tour_player_t real_player = {
    .get_status     = audio_player_get_status,
    .play           = audio_player_play,
    .load_paused    = audio_player_load_paused,
    .toggle_pause   = audio_player_toggle_pause,
    .stop           = audio_player_stop,
    .adjust_volume  = audio_player_adjust_volume,
    .seek_relative  = audio_player_seek_relative,
    .get_last_error = audio_player_get_last_error,
};

// The real screen, as seen by gui.c.
static const canvas_t screen = {
    .width         = RA8875_WIDTH,
    .height        = RA8875_HEIGHT,
    .fill_rect     = ra8875_fill_rect,
    .fill_triangle = ra8875_fill_triangle,
    .text          = ra8875_text,
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
    sigaction(SIGTERM, &action, NULL);      // e.g. "systemctl stop", or shutdown
    sigaction(SIGHUP, &action, NULL);       // e.g. the SSH session was closed
}

static long long now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000LL + now.tv_nsec / 1000000LL;
}

// ---------------------------------------------------------------------------
// Inputs -> queue. The encoder and joystick callbacks run on their own
// threads, so they only push into the thread-safe queue.
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
        app_log("  %c%c %2d  %s", i == view.highlighted ? '>' : ' ', i == view.playing ? '*' : ' ',
                i + 1, track_list_name(i));
    }
}

static void log_status(void)
{
    tour_view_t view = tour_controller_view();
    const char *popup = view.popup == TOUR_POPUP_GUIDE ? "   [directions open]"
                      : view.popup == TOUR_POPUP_TOUR_COMPLETE ? "   [tour complete open]" : "";
    if (view.mode == TOUR_MODE_CHOOSING) {
        app_log("[CHOOSING] highlighted %d/%d: %s   volume %d%%%s", view.highlighted + 1,
                view.track_count, view.highlighted >= 0 ? track_list_name(view.highlighted) : "-",
                view.audio.volume_percent, popup);
        return;
    }
    char time_text[32];
    playback_display_time(view.position_sec, view.audio.duration_sec, time_text, sizeof(time_text));
    app_log("[%s] %d. %s   %s%s   volume %d%%   highlighted %d%s", tour_controller_mode_name(view.mode),
            view.playing + 1, track_list_name(view.playing), time_text,
            view.scrubbing ? " (skipping)" : "", view.audio.volume_percent, view.highlighted + 1, popup);
}

static void handle_event(control_event_t event)
{
    switch (event.type) {
    case CONTROL_STATUS: log_status(); break;
    case CONTROL_LIST:   log_track_list(); break;
    case CONTROL_HELP:   app_log("%s", terminal_input_help_text()); break;
    case CONTROL_QUIT:   stop_requested = 1; break;
    case CONTROL_UNKNOWN: app_log("Unknown command. Type h for help."); break;
    case CONTROL_NONE:   break;
    default:             tour_controller_handle(event); break;
    }
}

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------
static void take_snapshot(saved_state_t *state)
{
    tour_view_t view = tour_controller_view();
    saved_state_clear(state);
    if (view.playing >= 0) {
        snprintf(state->track, sizeof(state->track), "%s", track_list_file_name(view.playing));
        state->position_sec = view.audio.position_sec;      // the real position, not a skip preview
    }
    state->volume_percent = view.audio.volume_percent;
    if (view.highlighted >= 0) {
        snprintf(state->highlighted, sizeof(state->highlighted), "%s", track_list_file_name(view.highlighted));
    }
}

static saved_state_t last_saved;
static long long last_save_ms = 0;

static void autosave(const char *path, bool force)
{
    saved_state_t now_state;
    take_snapshot(&now_state);
    long long now = now_ms();
    if (force || saved_state_should_save(&last_saved, &now_state, now - last_save_ms)) {
        if (saved_state_save(path, &now_state)) {
            last_saved = now_state;
            last_save_ms = now;
        } else if (force || now - last_save_ms > 60000) {
            app_log("Could not save the tour state to '%s'.", path);
            last_save_ms = now;            // do not repeat the message every frame
        }
    }
}

static void restore(const char *path)
{
    saved_state_t saved;
    if (!saved_state_load(path, &saved)) {
        saved_state_clear(&last_saved);
        return;
    }
    if (saved.volume_percent >= 0) {
        audio_player_set_volume(saved.volume_percent);
    }
    tour_controller_set_highlight(track_list_find(saved.highlighted));
    int track = track_list_find(saved.track);
    if (saved.track[0] != '\0' && track < 0) {
        app_log("The saved stop '%s' is no longer in the audio folder.", saved.track);
    } else if (track >= 0 && !tour_controller_resume(track, saved.position_sec)) {
        app_log("Could not resume '%s'.", track_list_name(track));
    }
    last_saved = saved;
}

// ---------------------------------------------------------------------------
// Start-up
// ---------------------------------------------------------------------------
static void exe_folder(char *out, size_t out_size)
{
    char exe_path[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (length > 0) {
        exe_path[length] = '\0';
        char *last_slash = strrchr(exe_path, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';
            snprintf(out, out_size, "%s", exe_path);
            return;
        }
    }
    snprintf(out, out_size, ".");
}

static void print_usage(const char *program)
{
    printf("Usage: %s [options]\n", program);
    printf("  --audio-dir DIR   folder with the .mp3 (and .txt directions) files\n");
    printf("                    (default: audio/ next to the program, or $TOUR_AUDIO_DIR)\n");
    printf("  --device DEVICE   ALSA device, e.g. plughw:1,0 (default: the USB adapter,\n");
    printf("                    or $TOUR_AUDIO_DEVICE)\n");
    printf("  --log FILE        also append every message to FILE\n");
    printf("  --state FILE      where to save the position (default: tour_state.txt next to\n");
    printf("                    the program)\n");
    printf("  --no-display      run without the screen\n");
    printf("  --spi-hz HZ       screen SPI clock (default %d)\n", RA8875_DEFAULT_SPI_HZ);
}

int main(int argc, char *argv[])
{
    char folder[PATH_MAX];
    exe_folder(folder, sizeof(folder));
    char audio_dir[PATH_MAX + 8];
    char state_path[PATH_MAX + 16];
    const char *from_env = getenv("TOUR_AUDIO_DIR");
    if (from_env != NULL && from_env[0] != '\0') {
        snprintf(audio_dir, sizeof(audio_dir), "%s", from_env);
    } else {
        snprintf(audio_dir, sizeof(audio_dir), "%s/audio", folder);
    }
    snprintf(state_path, sizeof(state_path), "%s/tour_state.txt", folder);
    const char *requested_device = getenv("TOUR_AUDIO_DEVICE");
    const char *log_path = NULL;
    bool use_display = true;
    unsigned long spi_hz = RA8875_DEFAULT_SPI_HZ;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--audio-dir") && i + 1 < argc) {
            snprintf(audio_dir, sizeof(audio_dir), "%s", argv[++i]);
        } else if (!strcmp(argv[i], "--device") && i + 1 < argc) {
            requested_device = argv[++i];
        } else if (!strcmp(argv[i], "--log") && i + 1 < argc) {
            log_path = argv[++i];
        } else if (!strcmp(argv[i], "--state") && i + 1 < argc) {
            snprintf(state_path, sizeof(state_path), "%s", argv[++i]);
        } else if (!strcmp(argv[i], "--no-display")) {
            use_display = false;
        } else if (!strcmp(argv[i], "--spi-hz") && i + 1 < argc) {
            spi_hz = strtoul(argv[++i], NULL, 10);
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

    // Each device is optional: without it the program still runs (typed commands work).
    if (!Encoder_init(on_encoder_turn, on_encoder_press)) {
        app_log("Encoder not available - use typed commands instead (h for help).");
    }
    if (!joystick_input_start(queue_event)) {
        app_log("Joystick not available - use typed commands instead (h for help).");
    }
    bool have_display = false;
    if (use_display) {
        have_display = ra8875_init((uint32_t)spi_hz);
        if (have_display) {
            gui_init(&screen);
        } else {
            app_log("Screen not available - running without it.");
        }
    }

    tour_controller_init(&real_player);
    app_log("=== SFU Burnaby Campus Tour ===");
    restore(state_path);
    log_track_list();
    app_log("Turn the knob to choose a stop, press it to play. Type h for the typed commands.");

    bool stdin_open = true;
    long long last_refresh_ms = now_ms();
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
        // Typed input: translated and queued like device input.
        if (ready > 0 && stdin_open && (watch[1].revents & (POLLIN | POLLHUP))) {
            if (!terminal_input_read(queue_event)) {
                stdin_open = false;       // e.g. running as a service: devices only
            }
        }

        control_event_t event;
        while (!stop_requested && input_queue_pop(&event)) {
            handle_event(event);
        }
        tour_controller_tick();
        if (have_display) {
            tour_view_t view = tour_controller_view();
            gui_render(&view);
            long long now = now_ms();
            if (now - last_refresh_ms >= GUI_REFRESH_STEP_MS) {   // quiet background repair
                gui_refresh_step(&view);
                last_refresh_ms = now;
            }
        }
        autosave(state_path, false);
    }

    autosave(state_path, true);
    app_log("Goodbye!");
    joystick_input_stop();      // stop the threads that push into the queue first
    Encoder_cleanup();
    if (have_display) {
        ra8875_cleanup();
    }
    audio_player_cleanup();
    input_queue_cleanup();
    track_list_cleanup();
    app_log_cleanup();
    return 0;
}
