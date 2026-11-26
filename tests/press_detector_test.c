// Host-side test of the encoder button logic (debounce, short and long press).
#include <stdio.h>
#include "hal/press_detector.h"

static int failures = 0;

static void expect(press_event_t actual, press_event_t wanted, const char *description)
{
    static const char *names[] = {"NONE", "SHORT", "LONG"};
    int ok = (actual == wanted);
    printf("  [%s] %s (got %s)\n", ok ? "PASS" : "FAIL", description, names[actual]);
    if (!ok) {
        failures++;
    }
}

// Hold the level from `from` to `to` ms (inclusive, 10 ms samples) and return
// the first non-NONE event seen (or NONE).
static press_event_t hold(press_detector_t *d, int pressed, long long from, long long to)
{
    press_event_t seen = PRESS_NONE;
    for (long long t = from; t <= to; t += 10) {
        press_event_t e = press_detector_update(d, pressed != 0, t);
        if (seen == PRESS_NONE) {
            seen = e;
        }
    }
    return seen;
}

int main(void)
{
    press_detector_t d;
    printf("Button press detection (debounce 20 ms, long press 1000 ms)\n");

    press_detector_init(&d, 20, 1000);
    expect(hold(&d, 0, 0, 500), PRESS_NONE, "idle: nothing");
    expect(hold(&d, 1, 510, 800), PRESS_NONE, "held 300 ms: nothing yet");
    expect(hold(&d, 0, 810, 900), PRESS_SHORT, "released after 300 ms: SHORT");

    expect(hold(&d, 1, 1000, 1990), PRESS_NONE, "held 990 ms: not long yet");
    expect(hold(&d, 1, 2000, 2000), PRESS_LONG, "held 1000 ms: LONG (while still held)");
    expect(hold(&d, 1, 2010, 3000), PRESS_NONE, "keep holding: LONG only once");
    expect(hold(&d, 0, 3010, 3100), PRESS_NONE, "release after LONG: no SHORT");

    // Bounce: 10 ms blips shorter than the 20 ms debounce are ignored.
    press_detector_init(&d, 20, 1000);
    press_event_t e1 = press_detector_update(&d, true, 0);
    press_event_t e2 = press_detector_update(&d, false, 10);
    press_event_t e3 = press_detector_update(&d, true, 20);
    press_event_t e4 = press_detector_update(&d, false, 30);
    press_event_t e5 = hold(&d, 0, 40, 200);
    expect(e1 != PRESS_NONE ? e1 : e2 != PRESS_NONE ? e2 : e3 != PRESS_NONE ? e3 : e4 != PRESS_NONE ? e4 : e5,
           PRESS_NONE, "contact bounce shorter than debounce: nothing");

    // A bouncy press followed by a steady one still gives one SHORT.
    press_detector_init(&d, 20, 1000);
    press_detector_update(&d, true, 0);
    press_detector_update(&d, false, 5);
    expect(hold(&d, 1, 10, 200), PRESS_NONE, "bouncy then steady press: pressed");
    press_detector_update(&d, false, 210);
    press_detector_update(&d, true, 215);
    expect(hold(&d, 0, 220, 400), PRESS_SHORT, "bouncy release: exactly one SHORT");

    printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
           failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
