#include "joystick_input.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Pure logic
// ---------------------------------------------------------------------------
void joystick_logic_init(joystick_logic_t *logic)
{
    logic->held = JOYSTICK_NONE;
    logic->next_repeat_ms = 0;
    press_detector_init(&logic->button, JOYSTICK_BUTTON_DEBOUNCE_MS, 1000);
}

static bool is_seek(joystick_direction_t d) { return d == JOYSTICK_LEFT || d == JOYSTICK_RIGHT; }

static control_event_t event_for(joystick_direction_t direction)
{
    control_event_t event = { CONTROL_NONE, 0 };
    switch (direction) {
    case JOYSTICK_UP:    event.type = CONTROL_VOLUME;    event.steps = +1; break;
    case JOYSTICK_DOWN:  event.type = CONTROL_VOLUME;    event.steps = -1; break;
    case JOYSTICK_RIGHT: event.type = CONTROL_SEEK_STEP; event.steps = +1; break;
    case JOYSTICK_LEFT:  event.type = CONTROL_SEEK_STEP; event.steps = -1; break;
    default: break;
    }
    return event;
}

static int repeat_ms(joystick_direction_t direction)
{
    return is_seek(direction) ? JOYSTICK_SEEK_REPEAT_MS : JOYSTICK_VOLUME_REPEAT_MS;
}

int joystick_logic_update(joystick_logic_t *logic, joystick_direction_t direction,
                          int button, long long now_ms, control_event_t *out, int max)
{
    int count = 0;
#define EMIT(e) do { if (count < max) out[count++] = (e); } while (0)

    if (direction != logic->held) {
        // Leaving a skip direction: jump to the previewed spot.
        if (is_seek(logic->held)) {
            control_event_t release = { CONTROL_SEEK_RELEASE, 0 };
            EMIT(release);
        }
        logic->held = direction;
        if (direction != JOYSTICK_NONE) {
            EMIT(event_for(direction));                       // act at once ...
            logic->next_repeat_ms = now_ms + repeat_ms(direction);
        }
    } else if (direction != JOYSTICK_NONE && now_ms >= logic->next_repeat_ms) {
        EMIT(event_for(direction));                           // ... then repeat while held
        logic->next_repeat_ms += repeat_ms(direction);
        if (logic->next_repeat_ms <= now_ms) {                // fell behind (e.g. a slow sample)
            logic->next_repeat_ms = now_ms + repeat_ms(direction);
        }
    }

    if (button >= 0) {
        press_event_t press = press_detector_update(&logic->button, button == 1, now_ms);
        if (press == PRESS_SHORT || press == PRESS_LONG) {    // once per press, however long
            control_event_t guide = { CONTROL_GUIDE, 0 };
            EMIT(guide);
        }
    }
#undef EMIT
    return count;
}

// ---------------------------------------------------------------------------
// Sampling thread
// ---------------------------------------------------------------------------
static pthread_t thread;
static atomic_bool stop_requested = false;
static bool running = false;
static joystick_input_deliver_t deliver_event = NULL;

static long long now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000LL + now.tv_nsec / 1000000LL;
}

static void *sample_thread(void *arg)
{
    (void)arg;
    joystick_logic_t logic;
    joystick_logic_init(&logic);
    struct timespec period = { 0, JOYSTICK_SAMPLE_MS * 1000000L };

    while (!atomic_load(&stop_requested)) {
        joystick_direction_t direction = joystick_get_direction();
        int button = joystick_button_pressed();
        control_event_t events[4];
        int count = joystick_logic_update(&logic, direction, button, now_ms(), events, 4);
        for (int i = 0; i < count; i++) {
            deliver_event(events[i]);
        }
        nanosleep(&period, NULL);
    }
    return NULL;
}

bool joystick_input_start(joystick_input_deliver_t deliver)
{
    assert(!running && deliver != NULL);
    if (!joystick_init()) {
        return false;
    }
    if (!joystick_button_available()) {
        fprintf(stderr, "joystick: button (%s) not available - directions still work\n",
                JOYSTICK_BUTTON_LINE);
    }
    deliver_event = deliver;
    atomic_store(&stop_requested, false);

    sigset_t all, old;                        // signals go to the main thread only
    sigfillset(&all);
    pthread_sigmask(SIG_BLOCK, &all, &old);
    int result = pthread_create(&thread, NULL, sample_thread, NULL);
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    if (result != 0) {
        joystick_cleanup();
        return false;
    }
    running = true;
    return true;
}

void joystick_input_stop(void)
{
    if (!running) {
        return;
    }
    atomic_store(&stop_requested, true);
    pthread_join(thread, NULL);
    joystick_cleanup();
    running = false;
}
