// Host-side test of the joystick direction logic - no hardware needed.
// The real spi_adc module is replaced by a fake whose values the test sets.
//
//   cmake -S . -B build-host -DBUILD_HOST_TESTS=ON && cmake --build build-host
//   ./build-host/tests/joystick_logic_test

#include <stdio.h>
#include "hal/joystick.h"
#include "hal/spi_adc.h"

// ---- fake ADC ----
static int fake_values[SPI_ADC_CHANNEL_COUNT];
bool spi_adc_init(void) { return true; }
void spi_adc_cleanup(void) {}
int spi_adc_read(int channel) { return fake_values[channel]; }

static int failures = 0;

static void expect(joystick_direction_t actual, joystick_direction_t wanted, const char *description)
{
    bool ok = (actual == wanted);
    printf("  [%s] %s (got %s)\n", ok ? "PASS" : "FAIL", description, joystick_direction_name(actual));
    if (!ok) {
        failures++;
    }
}

static joystick_direction_t press(int x, int y)
{
    fake_values[JOYSTICK_X_CHANNEL] = x;
    fake_values[JOYSTICK_Y_CHANNEL] = y;
    return joystick_get_direction();
}

int main(void)
{
    const joystick_direction_t up = JOYSTICK_Y_UP_IS_HIGH ? JOYSTICK_UP : JOYSTICK_DOWN;
    const joystick_direction_t down = JOYSTICK_Y_UP_IS_HIGH ? JOYSTICK_DOWN : JOYSTICK_UP;
    const joystick_direction_t right = JOYSTICK_X_RIGHT_IS_HIGH ? JOYSTICK_RIGHT : JOYSTICK_LEFT;
    const joystick_direction_t left = JOYSTICK_X_RIGHT_IS_HIGH ? JOYSTICK_LEFT : JOYSTICK_RIGHT;
    int cx, cy;

    printf("Stick resting at mid-scale (2048, 2048)\n");
    fake_values[JOYSTICK_X_CHANNEL] = 2048;
    fake_values[JOYSTICK_Y_CHANNEL] = 2048;
    joystick_init();
    joystick_get_centre(&cx, &cy);
    expect(cx == 2048 && cy == 2048 ? JOYSTICK_NONE : JOYSTICK_UP, JOYSTICK_NONE, "resting position measured");
    expect(press(2048, 2048), JOYSTICK_NONE, "centred stick is none");
    expect(press(2600, 1500), JOYSTICK_NONE, "pushed part way (27%): dead zone, none");
    expect(press(2048, 3400), JOYSTICK_NONE, "Y pushed 66%: still in the dead zone");
    expect(press(2048, 3700), up, "Y pushed 81%: counts");
    expect(press(2048, 100), down, "Y pushed low");
    expect(press(3900, 2048), right, "X pushed high");
    expect(press(100, 2048), left, "X pushed low");
    expect(press(3300, 3900), up, "diagonal, Y larger -> Y direction");
    expect(press(3900, 3300), right, "diagonal, X larger -> X direction");
    joystick_cleanup();

    printf("Stick resting OFF-centre (X 2000, Y 2900) - the bug seen on the board\n");
    fake_values[JOYSTICK_X_CHANNEL] = 2000;
    fake_values[JOYSTICK_Y_CHANNEL] = 2900;
    joystick_init();
    expect(press(2000, 2900), JOYSTICK_NONE, "at rest: none (used to read as UP)");
    expect(press(2030, 2950), JOYSTICK_NONE, "at rest with noise: none");
    expect(press(2000, 3700), JOYSTICK_NONE, "pushed up to 3700 (67% of the way): dead zone");
    expect(press(2000, 3900), up, "pushed up to 3900: up");
    expect(press(2000, 600), down, "pushed down to 600: down");
    joystick_cleanup();

    printf("Stick held while starting (Y 3900): fall back to mid-scale\n");
    fake_values[JOYSTICK_X_CHANNEL] = 2048;
    fake_values[JOYSTICK_Y_CHANNEL] = 3900;
    joystick_init();
    joystick_get_centre(&cx, &cy);
    expect(cy == 2048 ? JOYSTICK_NONE : JOYSTICK_UP, JOYSTICK_NONE, "implausible resting value ignored");

    fake_values[JOYSTICK_X_CHANNEL] = -1;
    fake_values[JOYSTICK_Y_CHANNEL] = 3900;
    expect(joystick_get_direction(), JOYSTICK_NONE, "read error reports none");
    expect(joystick_direction_from_raw(-1, 3900), JOYSTICK_NONE, "from_raw: failed read -> none");
    joystick_cleanup();

    printf("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
