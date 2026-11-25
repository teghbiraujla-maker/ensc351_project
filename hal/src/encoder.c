#include "hal/encoder.h"
#include <assert.h>
#include <gpiod.h> // GPIO controls
#include <pthread.h> // Thread controls
#include <stdatomic.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
// The three GPIO input pins being used
#define ENCODER_LINE_A_NAME "GPIO16"
#define ENCODER_LINE_B_NAME "GPIO17" 
#define ENCODER_BUTTON_NAME "GPIO18"
#define BUTTON_DEBOUNCE_MS 100

#define ENCODER_CONSUMER_NAME "beagley-encoder"
#define GPIO_CHIP_PATH_FORMAT "/dev/gpiochip%d"
#define GPIO_CHIP_PATH_LENGTH 32
#define MAX_GPIO_CHIPS_TO_SEARCH 16
#define ENCODER_LINE_COUNT 2
#define EVENT_BUFFER_CAPACITY 64
#define EDGE_WAIT_TIMEOUT_NS 100000000LL
#define INVERT_DIRECTION false

enum {STATE_START, STATE_CW_FINAL, STATE_CW_BEGIN, 
        STATE_CW_NEXT, STATE_CCW_BEGIN, STATE_CCW_FINAL,
        STATE_CCW_NEXT, STATE_COUNT};

#define STATE_MASK 0x0F
#define RESULT_NONE 0x00
#define RESULT_CW 0x10
#define RESULT_CCW 0x20
#define RESULT_MASK 0x30
#define LEVEL_COMBINATIONS 4
#define LEVEL_B_SHIFT 1

static const unsigned char STATE_TABLE[STATE_COUNT][LEVEL_COMBINATIONS] = {
    {STATE_START, STATE_CW_BEGIN, STATE_CCW_BEGIN, STATE_START},
    {STATE_CW_NEXT, STATE_START, STATE_CW_FINAL, STATE_START | RESULT_CW},
    {STATE_CW_NEXT, STATE_CW_BEGIN, STATE_START, STATE_START},
    {STATE_CW_NEXT, STATE_CW_BEGIN, STATE_CW_FINAL, STATE_START},
    {STATE_CCW_NEXT, STATE_START, STATE_CCW_BEGIN, STATE_START},
    {STATE_CCW_NEXT, STATE_CCW_FINAL, STATE_START, STATE_START | RESULT_CCW},
    {STATE_CCW_NEXT, STATE_CCW_FINAL, STATE_CCW_BEGIN, STATE_START},
};

// Initialize variables and structs here
// static, so these don't leave this specific file
static struct gpiod_chip *s_chip = NULL;
static struct gpiod_line_request *s_request = NULL;
static struct gpiod_edge_event_buffer *s_eventBuffer = NULL;

static unsigned int s_offsetA = 0;
static unsigned int s_offsetB = 0;
static unsigned int s_levelA = 1;
static unsigned int s_levelB = 1;
static unsigned char s_state = STATE_START;

static Encoder_callback_t s_onTurn = NULL;
static Encoder_button_callback_t s_onPress = NULL;
static pthread_t s_thread;
static atomic_bool s_stopRequested = false;
static bool s_isInitialized = false;

static bool findChipWithEncoderLines(void);
static struct gpiod_line_request *requestEncoderLines(bool usePullUp);
static void *encoderThread(void *arg);
static void handleEvents(int eventCount);
static int stepStateMachine(unsigned int levelA, unsigned int levelB);

static struct gpiod_line_request *s_buttonRequest = NULL;
static struct gpiod_edge_event_buffer *s_buttonEventBuffer = NULL;
static unsigned int s_buttonOffset = 0;
static struct timespec s_lastPressTime = {0, 0};
static bool findButtonLine(void);
static struct gpiod_line_request *requestButtonLine(bool usePullUp);
static long elapsedMs(const struct timespec *now, const struct timespec *prev);


static void releaseResources(void) {
    if (s_eventBuffer != NULL) {gpiod_edge_event_buffer_free(s_eventBuffer); s_eventBuffer = NULL;}
    if (s_request != NULL) {gpiod_line_request_release(s_request); s_request = NULL;}
    if (s_buttonEventBuffer != NULL) {gpiod_edge_event_buffer_free(s_buttonEventBuffer); s_buttonEventBuffer = NULL;}
    if (s_buttonRequest != NULL) {gpiod_line_request_release(s_buttonRequest); s_buttonRequest = NULL;}
    if (s_chip != NULL) {gpiod_chip_close(s_chip); s_chip = NULL;}
}

// Define and write out initialization function
bool Encoder_init(Encoder_callback_t onTurn, Encoder_button_callback_t onPress) {
    assert(!s_isInitialized);
    assert(onTurn != NULL);
    s_onTurn = onTurn;
    s_onPress = onPress;
    s_lastPressTime = (struct timespec){0, 0};

    if (!findChipWithEncoderLines()) {
        fprintf(stderr, "ERROR: GPIO %s and %s not found anywhere\n", 
        ENCODER_LINE_A_NAME, ENCODER_LINE_B_NAME);
        return false;
    }

    s_request = requestEncoderLines(true);
    if (s_request == NULL) {
        s_request = requestEncoderLines(false);
    }
    if (s_request == NULL) {
        perror("Unable to request GPIO lines");
        releaseResources();
        return false;
    }

    s_eventBuffer = gpiod_edge_event_buffer_new(EVENT_BUFFER_CAPACITY);
    if (s_eventBuffer == NULL) {
        releaseResources();
        return false;
    }

    s_levelA = (gpiod_line_request_get_value(s_request, s_offsetA) == GPIOD_LINE_VALUE_ACTIVE);
    s_levelB = (gpiod_line_request_get_value(s_request, s_offsetB) == GPIOD_LINE_VALUE_ACTIVE);
    s_state = STATE_START;

    if (s_onPress != NULL && findButtonLine()) {
        s_buttonRequest = requestButtonLine(true);
        if (s_buttonRequest == NULL) {
            s_buttonRequest = requestButtonLine(false);
        }
        if (s_buttonRequest != NULL) {
            s_buttonEventBuffer = gpiod_edge_event_buffer_new(EVENT_BUFFER_CAPACITY);
            if (s_buttonEventBuffer == NULL) {
                gpiod_line_request_release(s_buttonRequest);
                s_buttonRequest = NULL;
            }
        }
    }

    atomic_store(&s_stopRequested, false);

    sigset_t allSignals, oldMask;
    sigfillset(&allSignals);
    pthread_sigmask(SIG_BLOCK, &allSignals, &oldMask);
    int result = pthread_create(&s_thread, NULL, encoderThread, NULL);
    pthread_sigmask(SIG_SETMASK, &oldMask, NULL);

    if (result != 0) {
        fprintf(stderr, "ERROR: could not start encoder thread\n");
        releaseResources();
        return false;
    }

    s_isInitialized = true;
    return true;
}

// Cleanup function is here
void Encoder_cleanup(void) {
    assert(s_isInitialized);
    atomic_store(&s_stopRequested, true);
    pthread_join(s_thread, NULL);
    releaseResources();

    s_onTurn = NULL;
    s_onPress = NULL;

    s_isInitialized = false;
}

static bool findChipWithEncoderLines(void) {
    for (int chipNumber = 0; chipNumber < MAX_GPIO_CHIPS_TO_SEARCH; chipNumber++) {
        char path[GPIO_CHIP_PATH_LENGTH];
        snprintf(path, sizeof(path), GPIO_CHIP_PATH_FORMAT, chipNumber);
        struct gpiod_chip *chip = gpiod_chip_open(path);
        if (chip == NULL) {
            continue;
        }
        int offsetA = gpiod_chip_get_line_offset_from_name(chip, ENCODER_LINE_A_NAME);
        int offsetB = gpiod_chip_get_line_offset_from_name(chip, ENCODER_LINE_B_NAME);
        if (offsetA >= 0 && offsetB >= 0) {
            s_chip = chip;
            s_offsetA = (unsigned int)offsetA;
            s_offsetB = (unsigned int)offsetB;
            return true;
        }
        gpiod_chip_close(chip);
    }
    return false;
}

static struct gpiod_line_request *requestEncoderLines(bool usePullUp) {
    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    struct gpiod_line_config *lineConfig = gpiod_line_config_new();
    struct gpiod_request_config *requestConfig = gpiod_request_config_new();
    assert(settings != NULL && lineConfig != NULL && requestConfig != NULL);

    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_BOTH);
    if (usePullUp) {
        gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_UP);
    }

    unsigned int offsets[ENCODER_LINE_COUNT] = {s_offsetA, s_offsetB};
    gpiod_line_config_add_line_settings(lineConfig, offsets, ENCODER_LINE_COUNT, settings);
    gpiod_request_config_set_consumer(requestConfig, ENCODER_CONSUMER_NAME);
    gpiod_request_config_set_event_buffer_size(requestConfig, EVENT_BUFFER_CAPACITY);
    struct gpiod_line_request *request = gpiod_chip_request_lines(s_chip, requestConfig, lineConfig);

    gpiod_request_config_free(requestConfig);
    gpiod_line_config_free(lineConfig);
    gpiod_line_settings_free(settings);
    return request;
}

static void pollButton(void) {
    if (s_buttonRequest == NULL || s_buttonEventBuffer == NULL) {
        return;
    }

    int btnWait = gpiod_line_request_wait_edge_events(s_buttonRequest, 0);
    if (btnWait > 0) {
        int btnCount  = gpiod_line_request_read_edge_events(
            s_buttonRequest, s_buttonEventBuffer, EVENT_BUFFER_CAPACITY
        );
        if (btnCount > 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (elapsedMs(&now, &s_lastPressTime) >= BUTTON_DEBOUNCE_MS) {
                s_lastPressTime = now;
                if (s_onPress) {
                    s_onPress();
                }
            }
        }
    }
}

static void *encoderThread(void *arg) {
    (void)arg;
    while (!atomic_load(&s_stopRequested)) {
        pollButton();
        int waitResult = gpiod_line_request_wait_edge_events(s_request, EDGE_WAIT_TIMEOUT_NS);
        if (waitResult < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("Waiting for edge events");
            break;
        }
        if (waitResult == 0) {
            continue;
        }
        int eventCount = gpiod_line_request_read_edge_events(s_request, s_eventBuffer, EVENT_BUFFER_CAPACITY);
        if (eventCount < 0) {
            perror("Reading encoder edge events");
            break;
        }
        handleEvents(eventCount); // Helper function referred to below
        pollButton();
    }
    return NULL;
}

static void handleEvents(int eventCount) {
    for (int i = 0; i < eventCount; i++) {
        struct gpiod_edge_event *event = gpiod_edge_event_buffer_get_event(s_eventBuffer, (unsigned long)i);
        unsigned int offset = gpiod_edge_event_get_line_offset(event);
        unsigned int newLevel = 
            (gpiod_edge_event_get_event_type(event) == GPIOD_EDGE_EVENT_RISING_EDGE) ? 1 : 0;
        if (offset == s_offsetA) {
            s_levelA = newLevel;
        } else if (offset == s_offsetB) {
            s_levelB = newLevel;
        }

        int direction = stepStateMachine(s_levelA, s_levelB);
        if (direction != 0) {
            int finalDir = INVERT_DIRECTION ? -direction : direction;
            if (s_onTurn) {
                s_onTurn(finalDir);
            }
        }
    }
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

static long elapsedMs(const struct timespec *now, const struct timespec *prev) {
    long secDiff = now->tv_sec - prev->tv_sec;
    long nsecDiff = now->tv_nsec - prev->tv_nsec;
    return secDiff * 1000L + nsecDiff / 1000000LL;
}

static bool findButtonLine(void) {
    for (int chipNumber = 0; chipNumber < MAX_GPIO_CHIPS_TO_SEARCH; chipNumber++) {
        char path[GPIO_CHIP_PATH_LENGTH];
        snprintf(path, sizeof(path), GPIO_CHIP_PATH_FORMAT, chipNumber);
        struct gpiod_chip *chip = gpiod_chip_open(path);
        if (chip == NULL) {
            continue;
        }

        int offset = gpiod_chip_get_line_offset_from_name(chip, ENCODER_BUTTON_NAME);
        gpiod_chip_close(chip);
        if (offset >= 0) {
            s_buttonOffset = (unsigned int)offset;
            return true;
        }
    }
    return false;
}

static struct gpiod_line_request *requestButtonLine(bool usePullUp) {
    for (int chipNumber = 0; chipNumber < MAX_GPIO_CHIPS_TO_SEARCH; chipNumber++) {
        char path[GPIO_CHIP_PATH_LENGTH];
        snprintf(path, sizeof(path), GPIO_CHIP_PATH_FORMAT, chipNumber);
        struct gpiod_chip *c = gpiod_chip_open(path);
        if (c == NULL) {
            continue;
        }
        int offset = gpiod_chip_get_line_offset_from_name(c, ENCODER_BUTTON_NAME);
        if (offset == (int)s_buttonOffset) {
            struct gpiod_line_settings *settings = gpiod_line_settings_new();
            struct gpiod_line_config *lineConfig = gpiod_line_config_new();
            struct gpiod_request_config *requestConfig = gpiod_request_config_new();
            assert(settings != NULL && lineConfig != NULL && requestConfig != NULL);
            gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
            gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_FALLING);
            if (usePullUp) {
                gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_UP);
            }

            gpiod_line_config_add_line_settings(lineConfig, &s_buttonOffset, 1, settings);
            gpiod_request_config_set_consumer(requestConfig, ENCODER_CONSUMER_NAME "-btn");
            gpiod_request_config_set_event_buffer_size(requestConfig, EVENT_BUFFER_CAPACITY);

            struct gpiod_line_request *req = gpiod_chip_request_lines(c, requestConfig, lineConfig);
            gpiod_request_config_free(requestConfig);
            gpiod_line_config_free(lineConfig);
            gpiod_line_settings_free(settings);
            gpiod_chip_close(c);
            return req;
        }
        gpiod_chip_close(c);
    }
    return NULL;
}
