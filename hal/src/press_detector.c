#include "hal/press_detector.h"
#include <assert.h>
#include <stddef.h>

void press_detector_init(press_detector_t *detector, int debounce_ms, int long_press_ms)
{
    assert(detector != NULL);
    assert(debounce_ms >= 0 && long_press_ms > debounce_ms);
    detector->debounce_ms = debounce_ms;
    detector->long_press_ms = long_press_ms;
    detector->raw = false;
    detector->raw_since_ms = 0;
    detector->stable = false;
    detector->press_start_ms = 0;
    detector->long_fired = false;
}

press_event_t press_detector_update(press_detector_t *detector, bool pressed, long long now_ms)
{
    // 1. Remember when the raw level last changed.
    if (pressed != detector->raw) {
        detector->raw = pressed;
        detector->raw_since_ms = now_ms;
    }

    // 2. Accept the new level once it has been steady for debounce_ms.
    if (detector->raw != detector->stable &&
        now_ms - detector->raw_since_ms >= detector->debounce_ms) {
        detector->stable = detector->raw;
        if (detector->stable) {
            detector->press_start_ms = detector->raw_since_ms;   // when it really went down
            detector->long_fired = false;
        } else {
            bool was_long = detector->long_fired;
            detector->long_fired = false;
            if (!was_long) {
                return PRESS_SHORT;
            }
        }
    }

    // 3. Held long enough: report LONG once, without waiting for release.
    if (detector->stable && !detector->long_fired &&
        now_ms - detector->press_start_ms >= detector->long_press_ms) {
        detector->long_fired = true;
        return PRESS_LONG;
    }
    return PRESS_NONE;
}
