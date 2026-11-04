#include "hal/joystick.h"
#include "hal/spi_adc.h"
#include <assert.h>

// Starting guesses for each axis (12-bit ADC resting near the middle);
// the range grows automatically as the stick is moved.
#define INITIAL_MIN 1000
#define INITIAL_MAX 3000

// Distance from centre that counts as "pressed" (0.5 would be full deflection).
// 0.40 means 80% of the way to the extreme.
#define PRESS_DEFLECTION 0.40

typedef struct {
    int channel;
    int min;
    int max;
} axis_t;

static bool is_initialized = false;
static axis_t x_axis;
static axis_t y_axis;

static void axis_init(axis_t *axis, int channel)
{
    axis->channel = channel;
    axis->min = INITIAL_MIN;
    axis->max = INITIAL_MAX;
}

// Returns the position 0.0 (min) .. 1.0 (max), widening the calibrated range
// first if this reading is outside it.
static double axis_normalize(axis_t *axis, int value)
{
    if (value < axis->min) {
        axis->min = value;
    }
    if (value > axis->max) {
        axis->max = value;
    }
    return (double)(value - axis->min) / (double)(axis->max - axis->min);
}

bool joystick_init(void)
{
    assert(!is_initialized);
    if (!spi_adc_init()) {
        return false;
    }
    axis_init(&x_axis, JOYSTICK_X_CHANNEL);
    axis_init(&y_axis, JOYSTICK_Y_CHANNEL);
    is_initialized = true;
    return true;
}

void joystick_cleanup(void)
{
    assert(is_initialized);
    spi_adc_cleanup();
    is_initialized = false;
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

    // Signed deflection from centre; positive = toward the "high" ADC end.
    double x = axis_normalize(&x_axis, raw_x) - 0.5;
    double y = axis_normalize(&y_axis, raw_y) - 0.5;
    double abs_x = (x < 0) ? -x : x;
    double abs_y = (y < 0) ? -y : y;

    bool x_pressed = abs_x >= PRESS_DEFLECTION;
    bool y_pressed = abs_y >= PRESS_DEFLECTION;
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
