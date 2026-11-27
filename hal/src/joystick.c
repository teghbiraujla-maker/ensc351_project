#include "hal/joystick.h"
#include "hal/gpio.h"
#include "hal/spi_adc.h"
#include <assert.h>
#include <stddef.h>

// Where the stick rests is measured when joystick_init() runs (the stick
// must not be touched then). Many sticks rest well away from mid-scale
// (2048), so "centre" is that measured resting value, not 2048.
#define CENTRE_SAMPLES   16
#define CENTRE_MIN_OK    1024     // a resting value outside 1024..3071 means
#define CENTRE_MAX_OK    3071     // someone was holding the stick: use 2048

typedef struct {
    int channel;
    int centre;
} axis_t;

static bool is_initialized = false;
static gpio_line_t *button = NULL;
static axis_t x_axis;
static axis_t y_axis;

static int measure_centre(int channel)
{
    long sum = 0;
    int good = 0;
    for (int i = 0; i < CENTRE_SAMPLES; i++) {
        int value = spi_adc_read(channel);
        if (value >= 0) {
            sum += value;
            good++;
        }
    }
    int centre = (good > 0) ? (int)(sum / good) : (SPI_ADC_MAX_VALUE + 1) / 2;
    if (centre < CENTRE_MIN_OK || centre > CENTRE_MAX_OK) {
        centre = (SPI_ADC_MAX_VALUE + 1) / 2;
    }
    return centre;
}

static void axis_init(axis_t *axis, int channel)
{
    axis->channel = channel;
    axis->centre = measure_centre(channel);
}

// How far the stick is pushed, -1.0 .. +1.0, measured from its resting
// position to the end of travel on that side:
//   above centre: (value - centre) / (4095 - centre)
//   below centre: (value - centre) / centre
static double axis_deflection(const axis_t *axis, int value)
{
    if (value >= axis->centre) {
        return (double)(value - axis->centre) / (double)(SPI_ADC_MAX_VALUE - axis->centre);
    }
    return (double)(value - axis->centre) / (double)axis->centre;
}

bool joystick_init(void)
{
    assert(!is_initialized);
    if (!spi_adc_init()) {
        return false;
    }
    axis_init(&x_axis, JOYSTICK_X_CHANNEL);
    axis_init(&y_axis, JOYSTICK_Y_CHANNEL);
    button = gpio_open_input(JOYSTICK_BUTTON_LINE, "tour-joystick-btn", true, false);
    is_initialized = true;
    return true;
}

void joystick_cleanup(void)
{
    assert(is_initialized);
    gpio_close(button);
    button = NULL;
    spi_adc_cleanup();
    is_initialized = false;
}

int joystick_button_pressed(void)
{
    if (button == NULL) {
        return -1;
    }
    int level = gpio_get(button);
    if (level < 0) {
        return -1;
    }
    return (level == 0) ? 1 : 0;      // active low
}

bool joystick_button_available(void)
{
    return button != NULL;
}

void joystick_get_centre(int *centre_x, int *centre_y)
{
    assert(is_initialized);
    *centre_x = x_axis.centre;
    *centre_y = y_axis.centre;
}

int joystick_read_raw_x(void)
{
    assert(is_initialized);
    return spi_adc_read(JOYSTICK_X_CHANNEL);
}

int joystick_read_raw_y(void)
{
    assert(is_initialized);
    return spi_adc_read(JOYSTICK_Y_CHANNEL);
}

double joystick_raw_to_volts(int raw)
{
    return (double)raw * JOYSTICK_VREF_VOLTS / (double)(SPI_ADC_MAX_VALUE + 1);
}

joystick_direction_t joystick_direction_from_raw(int raw_x, int raw_y)
{
    assert(is_initialized);
    if (raw_x < 0 || raw_y < 0) {
        return JOYSTICK_NONE;
    }

    // Signed deflection from the resting position; positive = toward the "high" ADC end.
    double x = axis_deflection(&x_axis, raw_x);
    double y = axis_deflection(&y_axis, raw_y);
    double abs_x = (x < 0) ? -x : x;
    double abs_y = (y < 0) ? -y : y;

    // Large dead zone: nothing counts until the stick is most of the way out.
    bool x_pressed = abs_x >= JOYSTICK_PRESS_FRACTION;
    bool y_pressed = abs_y >= JOYSTICK_PRESS_FRACTION;
    if (!x_pressed && !y_pressed) {
        return JOYSTICK_NONE;
    }
    if (y_pressed && (!x_pressed || abs_y >= abs_x)) {
        bool is_high = (y > 0);
        return (is_high == (JOYSTICK_Y_UP_IS_HIGH != 0)) ? JOYSTICK_UP : JOYSTICK_DOWN;
    }
    bool is_high = (x > 0);
    return (is_high == (JOYSTICK_X_RIGHT_IS_HIGH != 0)) ? JOYSTICK_RIGHT : JOYSTICK_LEFT;
}

joystick_direction_t joystick_get_direction(void)
{
    assert(is_initialized);
    return joystick_direction_from_raw(joystick_read_raw_x(), joystick_read_raw_y());
}

const char *joystick_direction_name(joystick_direction_t direction)
{
    switch (direction) {
    case JOYSTICK_UP:    return "UP";
    case JOYSTICK_DOWN:  return "DOWN";
    case JOYSTICK_LEFT:  return "LEFT";
    case JOYSTICK_RIGHT: return "RIGHT";
    default:             return "none";
    }
}
