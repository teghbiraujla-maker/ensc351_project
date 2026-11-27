// Host-side test of the joystick -> control event logic (repeat rates,
// skip release, button) - no ADC or GPIO needed.
#include <stdio.h>
#include "joystick_input.h"

static int failures = 0;

#define CHECK(condition, description) do { \
    int ok_ = (condition); \
    printf("  [%s] %s\n", ok_ ? "PASS" : "FAIL", description); \
    if (!ok_) { printf("         (line %d: %s)\n", __LINE__, #condition); failures++; } } while (0)

typedef struct { int volume; int seek; int releases; int guides; } tally_t;

// Hold `direction` and `button` from `from` to `to` ms in 10 ms samples.
static tally_t hold(joystick_logic_t *logic, joystick_direction_t direction, int button,
                    long long from, long long to)
{
    tally_t t = {0, 0, 0, 0};
    for (long long now = from; now <= to; now += 10) {
        control_event_t events[4];
        int n = joystick_logic_update(logic, direction, button, now, events, 4);
        for (int i = 0; i < n; i++) {
            if (events[i].type == CONTROL_VOLUME) t.volume += events[i].steps;
            if (events[i].type == CONTROL_SEEK_STEP) t.seek += events[i].steps;
            if (events[i].type == CONTROL_SEEK_RELEASE) t.releases++;
            if (events[i].type == CONTROL_GUIDE) t.guides++;
        }
    }
    return t;
}

int main(void)
{
    joystick_logic_t logic;
    joystick_logic_init(&logic);
    tally_t t;

    printf("Volume (up / down)\n");
    t = hold(&logic, JOYSTICK_UP, 0, 0, 0);
    CHECK(t.volume == 1, "a quick flick up: one step at once");
    t = hold(&logic, JOYSTICK_UP, 0, 10, 1990);
    CHECK(t.volume == 3, "held up 2 s in total: one step per 0.5 s (4 total)");
    t = hold(&logic, JOYSTICK_NONE, 0, 2000, 2500);
    CHECK(t.volume == 0 && t.releases == 0, "centred: nothing");
    t = hold(&logic, JOYSTICK_DOWN, 0, 2510, 3500);
    CHECK(t.volume == -2, "held down 1 s: two steps down");

    printf("Skipping (left / right)\n");
    hold(&logic, JOYSTICK_NONE, 0, 3510, 3600);
    t = hold(&logic, JOYSTICK_RIGHT, 0, 3610, 4600);
    CHECK(t.seek == 10 && t.releases == 0, "held right 1 s: 10 one-second steps, no release yet");
    t = hold(&logic, JOYSTICK_NONE, 0, 4610, 4700);
    CHECK(t.releases == 1, "back to centre: one release");
    t = hold(&logic, JOYSTICK_LEFT, 0, 4710, 5200);
    CHECK(t.seek == -5, "held left 0.5 s: 5 steps back");
    t = hold(&logic, JOYSTICK_UP, 0, 5210, 5210);
    CHECK(t.releases == 1 && t.volume == 1, "left straight to up: release, then volume");
    hold(&logic, JOYSTICK_NONE, 0, 5220, 5300);

    printf("Button\n");
    t = hold(&logic, JOYSTICK_NONE, 1, 5310, 5500);
    CHECK(t.guides == 0, "while pressed: nothing yet");
    t = hold(&logic, JOYSTICK_NONE, 0, 5510, 5600);
    CHECK(t.guides == 1, "released: one guide event");
    t = hold(&logic, JOYSTICK_NONE, 1, 5610, 8000);
    t.guides += hold(&logic, JOYSTICK_NONE, 0, 8010, 8100).guides;
    CHECK(t.guides == 1, "held a long time: still one guide event");
    t = hold(&logic, JOYSTICK_NONE, -1, 8110, 9000);
    CHECK(t.guides == 0, "no button wired: nothing");

    printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
           failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
