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

    joystick_init();
    printf("Joystick direction logic\n");

    expect(press(2048, 2048), JOYSTICK_NONE, "centred stick is none");
    expect(press(2300, 1800), JOYSTICK_NONE, "small wobble is none");
    expect(press(2048, 3900), up, "Y pushed high");
    expect(press(2048, 100), down, "Y pushed low");
    expect(press(3900, 2048), right, "X pushed high");
    expect(press(100, 2048), left, "X pushed low");
    expect(press(2048, 2048), JOYSTICK_NONE, "back to centre");

    // Diagonal: the larger deflection wins.
    expect(press(3300, 3900), up, "diagonal, Y larger -> Y direction");
    expect(press(3900, 3300), right, "diagonal, X larger -> X direction");
    expect(press(2048, 2048), JOYSTICK_NONE, "centre again");

    // The range seen so far is now about 100..3900, so 3000 is no longer "most of the way".
    expect(press(2048, 3000), JOYSTICK_NONE, "calibration widened: 3000 no longer counts as pressed");

    fake_values[JOYSTICK_X_CHANNEL] = -1;
    fake_values[JOYSTICK_Y_CHANNEL] = 3900;
    expect(joystick_get_direction(), JOYSTICK_NONE, "read error reports none");

    // Direction from readings taken earlier (what joystick_test uses)
    expect(joystick_direction_from_raw(2048, 3900), up, "from_raw: Y high");
    expect(joystick_direction_from_raw(-1, 3900), JOYSTICK_NONE, "from_raw: failed read -> none");

    joystick_cleanup();
    printf("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
