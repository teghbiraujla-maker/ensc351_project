#include "hal/encoder.h"
#include "hal/gpio.h"
#include "hal/press_detector.h"
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Wiring (by line name; see README "Wiring"). A and B are on DIFFERENT
// gpiochips on the BeagleY-AI, which is why each line is opened on its own.
#define ENCODER_LINE_A_NAME "GPIO17"    // header pin 11
#define ENCODER_LINE_B_NAME "GPIO27"    // header pin 13
#define ENCODER_BUTTON_NAME "GPIO22"    // header pin 15, other leg to GND
#define BUTTON_ACTIVE_LOW   true        // pressed = pin pulled to GND
#define BUTTON_DEBOUNCE_MS  20
#define INVERT_DIRECTION    false       // true if clockwise counts the wrong way

#define CONSUMER_NAME   "tour-encoder"
#define POLL_TIMEOUT_MS 10              // also how often the button is sampled
#define EDGE_BATCH      64

enum {STATE_START, STATE_CW_FINAL, STATE_CW_BEGIN,
        STATE_CW_NEXT, STATE_CCW_BEGIN, STATE_CCW_FINAL,
        STATE_CCW_NEXT, STATE_COUNT};

#define STATE_MASK 0x0F
#define RESULT_CW 0x10
#define RESULT_CCW 0x20
#define RESULT_MASK 0x30
#define LEVEL_COMBINATIONS 4
#define LEVEL_B_SHIFT 1

// Quadrature decoding table (one result per full detent).
static const unsigned char STATE_TABLE[STATE_COUNT][LEVEL_COMBINATIONS] = {
    {STATE_START, STATE_CW_BEGIN, STATE_CCW_BEGIN, STATE_START},
    {STATE_CW_NEXT, STATE_START, STATE_CW_FINAL, STATE_START | RESULT_CW},
    {STATE_CW_NEXT, STATE_CW_BEGIN, STATE_START, STATE_START},
    {STATE_CW_NEXT, STATE_CW_BEGIN, STATE_CW_FINAL, STATE_START},
    {STATE_CCW_NEXT, STATE_START, STATE_CCW_BEGIN, STATE_START},
    {STATE_CCW_NEXT, STATE_CCW_FINAL, STATE_START, STATE_START | RESULT_CCW},
    {STATE_CCW_NEXT, STATE_CCW_FINAL, STATE_CCW_BEGIN, STATE_START},
};

static gpio_line_t *s_lineA = NULL;
static gpio_line_t *s_lineB = NULL;
static gpio_line_t *s_button = NULL;
static unsigned int s_levelA = 1;
static unsigned int s_levelB = 1;
static unsigned char s_state = STATE_START;
static press_detector_t s_pressDetector;

static Encoder_callback_t s_onTurn = NULL;
static Encoder_button_callback_t s_onPress = NULL;
static pthread_t s_thread;
static atomic_bool s_stopRequested = false;
static bool s_isInitialized = false;

static void releaseResources(void) {
    gpio_close(s_lineA);
    gpio_close(s_lineB);
    gpio_close(s_button);
    s_lineA = s_lineB = s_button = NULL;
}

static long long nowMs(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000LL + now.tv_nsec / 1000000LL;
}

static int stepStateMachine(unsigned int levelA, unsigned int levelB) {
    unsigned int column = (levelB << LEVEL_B_SHIFT) | levelA;
    s_state = STATE_TABLE[s_state & STATE_MASK][column];
    switch (s_state & RESULT_MASK) {
        case RESULT_CW: return 1;
        case RESULT_CCW: return -1;
        default: return 0;
    }
}

// Apply one edge to the decoder and report a click if one completed.
static void applyEdge(bool isA, bool rising) {
    if (isA) {
        s_levelA = rising ? 1 : 0;
    } else {
        s_levelB = rising ? 1 : 0;
    }
    int direction = stepStateMachine(s_levelA, s_levelB);
    if (direction != 0 && s_onTurn) {
        s_onTurn(INVERT_DIRECTION ? -direction : direction);
    }
}

// A and B come from separate requests, so merge their edges by kernel
// timestamp: the decoder must see transitions in the order they happened.
static void handleEdges(void) {
    gpio_edge_t edgesA[EDGE_BATCH];
    gpio_edge_t edgesB[EDGE_BATCH];
    struct pollfd watch[2] = {
        { gpio_fd(s_lineA), POLLIN, 0 },
        { gpio_fd(s_lineB), POLLIN, 0 },
    };
    poll(watch, 2, 0);
    int countA = (watch[0].revents & POLLIN) ? gpio_read_edges(s_lineA, edgesA, EDGE_BATCH) : 0;
    int countB = (watch[1].revents & POLLIN) ? gpio_read_edges(s_lineB, edgesB, EDGE_BATCH) : 0;
    if (countA < 0) countA = 0;
    if (countB < 0) countB = 0;

    int a = 0, b = 0;
    while (a < countA || b < countB) {
        bool takeA = (b >= countB) || (a < countA && edgesA[a].timestamp_ns <= edgesB[b].timestamp_ns);
        if (takeA) {
            applyEdge(true, edgesA[a++].rising);
        } else {
            applyEdge(false, edgesB[b++].rising);
        }
    }
}

static void pollButton(void) {
    if (s_button == NULL) {
        return;
    }
    int level = gpio_get(s_button);
    if (level < 0) {
        return;
    }
    bool pressed = BUTTON_ACTIVE_LOW ? (level == 0) : (level == 1);
    press_event_t event = press_detector_update(&s_pressDetector, pressed, nowMs());
    if (event == PRESS_SHORT && s_onPress) {
        s_onPress(ENCODER_PRESS_SHORT);
    } else if (event == PRESS_LONG && s_onPress) {
        s_onPress(ENCODER_PRESS_LONG);
    }
}

static void *encoderThread(void *arg) {
    (void)arg;
    while (!atomic_load(&s_stopRequested)) {
        struct pollfd watch[2] = {
            { gpio_fd(s_lineA), POLLIN, 0 },
            { gpio_fd(s_lineB), POLLIN, 0 },
        };
        int ready = poll(watch, 2, POLL_TIMEOUT_MS);
        if (ready < 0 && errno != EINTR) {
            perror("encoder: poll");
            break;
        }
        if (ready > 0) {
            handleEdges();
        }
        pollButton();   // at least every POLL_TIMEOUT_MS
    }
    return NULL;
}

bool Encoder_init(Encoder_callback_t onTurn, Encoder_button_callback_t onPress) {
    assert(!s_isInitialized);
    assert(onTurn != NULL);
    s_onTurn = onTurn;
    s_onPress = onPress;

    s_lineA = gpio_open_input(ENCODER_LINE_A_NAME, CONSUMER_NAME, true, true);
    s_lineB = gpio_open_input(ENCODER_LINE_B_NAME, CONSUMER_NAME, true, true);
    if (s_lineA == NULL || s_lineB == NULL) {
        fprintf(stderr, "encoder: cannot open %s / %s\n", ENCODER_LINE_A_NAME, ENCODER_LINE_B_NAME);
        releaseResources();
        return false;
    }
    int levelA = gpio_get(s_lineA);
    int levelB = gpio_get(s_lineB);
    s_levelA = (levelA > 0) ? 1 : 0;
    s_levelB = (levelB > 0) ? 1 : 0;
    s_state = STATE_START;

    if (s_onPress != NULL) {
        s_button = gpio_open_input(ENCODER_BUTTON_NAME, CONSUMER_NAME "-btn", true, false);
        if (s_button == NULL) {
            fprintf(stderr, "encoder: button line %s not available - turning still works\n",
                    ENCODER_BUTTON_NAME);
        }
    }
    press_detector_init(&s_pressDetector, BUTTON_DEBOUNCE_MS, ENCODER_LONG_PRESS_MS);

    atomic_store(&s_stopRequested, false);
    // The thread must not receive Ctrl-C etc.; the main thread handles signals.
    sigset_t allSignals, oldMask;
    sigfillset(&allSignals);
    pthread_sigmask(SIG_BLOCK, &allSignals, &oldMask);
    int result = pthread_create(&s_thread, NULL, encoderThread, NULL);
    pthread_sigmask(SIG_SETMASK, &oldMask, NULL);
    if (result != 0) {
        fprintf(stderr, "encoder: could not start encoder thread\n");
        releaseResources();
        return false;
    }
    s_isInitialized = true;
    return true;
}

void Encoder_cleanup(void) {
    if (!s_isInitialized) {
        return;
    }
    atomic_store(&s_stopRequested, true);
    pthread_join(s_thread, NULL);
    releaseResources();
    s_onTurn = NULL;
    s_onPress = NULL;
    s_isInitialized = false;
}
