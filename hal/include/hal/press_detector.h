// press_detector.h
// Turns raw push-button samples into SHORT and LONG presses (no hardware here,
// so it can be unit tested on the host).
//
// Feed it the button level every few milliseconds:
//   - The level must stay the same for `debounce_ms` before it counts, so
//     contact bounce is ignored.
//   - LONG fires as soon as the button has been held for `long_press_ms`
//     (the user does not have to let go first).
//   - SHORT fires on release, but only if LONG did not already fire.

#ifndef _PRESS_DETECTOR_H_
#define _PRESS_DETECTOR_H_

#include <stdbool.h>

typedef enum {
    PRESS_NONE,
    PRESS_SHORT,
    PRESS_LONG,
} press_event_t;

typedef struct {
    int debounce_ms;
    int long_press_ms;
    bool raw;                  // last sample
    long long raw_since_ms;    // when the raw level last changed
    bool stable;               // debounced level (true = pressed)
    long long press_start_ms;  // when the current press began
    bool long_fired;           // LONG already reported for this press
} press_detector_t;

void press_detector_init(press_detector_t *detector, int debounce_ms, int long_press_ms);

// pressed: current button level. now_ms: a monotonic time in milliseconds.
press_event_t press_detector_update(press_detector_t *detector, bool pressed, long long now_ms);

#endif
