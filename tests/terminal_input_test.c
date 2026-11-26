// Host-side test: typed debug commands -> encoder-style control events.
#include <stdio.h>
#include "terminal_input.h"

static int failures = 0;

static void expect(const char *typed, control_type_t type, int steps)
{
    control_event_t e = terminal_input_translate(typed);
    int ok = (e.type == type) && (type != CONTROL_TURN || e.steps == steps);
    printf("  [%s] '%s' -> type %d, steps %d\n", ok ? "PASS" : "FAIL", typed, e.type, e.steps);
    if (!ok) {
        failures++;
    }
}

int main(void)
{
    printf("Typed commands\n");
    expect("+", CONTROL_TURN, 1);
    expect(".", CONTROL_TURN, 1);
    expect("+++", CONTROL_TURN, 3);
    expect("...", CONTROL_TURN, 3);
    expect("+5", CONTROL_TURN, 5);
    expect("-", CONTROL_TURN, -1);
    expect(",,", CONTROL_TURN, -2);
    expect("-12", CONTROL_TURN, -12);
    expect("+999", CONTROL_TURN, 100);        // clamped
    expect("  +  ", CONTROL_TURN, 1);         // spaces ignored
    expect("+-", CONTROL_UNKNOWN, 0);         // mixed directions
    expect("+0", CONTROL_UNKNOWN, 0);
    expect("p", CONTROL_PRESS, 0);
    expect("PRESS", CONTROL_PRESS, 0);
    expect("x", CONTROL_LONG_PRESS, 0);
    expect("hold", CONTROL_LONG_PRESS, 0);
    expect("i", CONTROL_STATUS, 0);
    expect("l", CONTROL_LIST, 0);
    expect("h", CONTROL_HELP, 0);
    expect("?", CONTROL_HELP, 0);
    expect("q", CONTROL_QUIT, 0);
    expect("", CONTROL_NONE, 0);
    expect("3", CONTROL_UNKNOWN, 0);          // track numbers are no longer a command
    expect("banana", CONTROL_UNKNOWN, 0);

    printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
           failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
