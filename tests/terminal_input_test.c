// Host-side test: typed debug commands -> control events.
#include <stdio.h>
#include "terminal_input.h"

static int failures = 0;

static void expect(const char *typed, control_type_t type, int steps)
{
    control_event_t e[2];
    int n = terminal_input_translate(typed, e, 2);
    int ok = (type == CONTROL_NONE) ? (n == 0)
             : (n == 1 && e[0].type == type && e[0].steps == steps);
    printf("  [%s] '%s' -> %d event(s), type %d, steps %d\n", ok ? "PASS" : "FAIL", typed, n,
           n > 0 ? (int)e[0].type : -1, n > 0 ? e[0].steps : 0);
    if (!ok) failures++;
}

static void expect_seek(const char *typed, int seconds)
{
    control_event_t e[2];
    int n = terminal_input_translate(typed, e, 2);
    int ok = (n == 2 && e[0].type == CONTROL_SEEK_STEP && e[0].steps == seconds
              && e[1].type == CONTROL_SEEK_RELEASE);
    printf("  [%s] '%s' -> skip %d s then release\n", ok ? "PASS" : "FAIL", typed, seconds);
    if (!ok) failures++;
}

int main(void)
{
    printf("Encoder commands\n");
    expect("+", CONTROL_TURN, 1);
    expect("+++", CONTROL_TURN, 3);
    expect("+5", CONTROL_TURN, 5);
    expect("-", CONTROL_TURN, -1);
    expect(",,", CONTROL_TURN, -2);
    expect("+999", CONTROL_TURN, 100);
    expect("+-", CONTROL_UNKNOWN, 0);
    expect("p", CONTROL_PRESS, 0);
    expect("x", CONTROL_LONG_PRESS, 0);
    expect("hold", CONTROL_LONG_PRESS, 0);

    printf("Joystick commands\n");
    expect("u", CONTROL_VOLUME, 1);
    expect("u3", CONTROL_VOLUME, 3);
    expect("d", CONTROL_VOLUME, -1);
    expect("D2", CONTROL_VOLUME, -2);
    expect_seek("f", 1);
    expect_seek("f20", 20);
    expect_seek("b", -1);
    expect_seek("b15", -15);
    expect("g", CONTROL_GUIDE, 0);
    expect("u0", CONTROL_UNKNOWN, 0);
    expect("fx", CONTROL_UNKNOWN, 0);

    printf("Debug commands\n");
    expect("i", CONTROL_STATUS, 0);
    expect("l", CONTROL_LIST, 0);
    expect("h", CONTROL_HELP, 0);
    expect("q", CONTROL_QUIT, 0);
    expect("", CONTROL_NONE, 0);
    expect("   ", CONTROL_NONE, 0);
    expect("banana", CONTROL_UNKNOWN, 0);

    printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
           failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
