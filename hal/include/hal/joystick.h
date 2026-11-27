// joystick.h
// HAL module for the analog joystick: X and Y axes read through the SPI ADC,
// and the push button (SEL) on a GPIO line.
//
// The stick's resting position is measured when joystick_init() runs (don't
// touch the stick then), and a direction only counts once the stick is pushed
// JOYSTICK_PRESS_FRACTION of the way from rest to the end of travel. The large
// dead zone keeps a stick that rests slightly off-centre from acting on its own.

#ifndef _JOYSTICK_H_
#define _JOYSTICK_H_

#include <stdbool.h>

// ---- Hardware wiring (override at build time with -D, see README) ----
#ifndef JOYSTICK_X_CHANNEL
#define JOYSTICK_X_CHANNEL 0     // ADC channel wired to the joystick's Xout
#endif
#ifndef JOYSTICK_Y_CHANNEL
#define JOYSTICK_Y_CHANNEL 1     // ADC channel wired to the joystick's Yout
#endif
// Does pushing the stick UP raise the ADC value? (1 = yes, 0 = no)
#ifndef JOYSTICK_Y_UP_IS_HIGH
#define JOYSTICK_Y_UP_IS_HIGH 1
#endif
// Does pushing the stick RIGHT raise the ADC value? (1 = yes, 0 = no)
#ifndef JOYSTICK_X_RIGHT_IS_HIGH
#define JOYSTICK_X_RIGHT_IS_HIGH 1
#endif

#define JOYSTICK_VREF_VOLTS 3.3

// Dead zone: how far (0..1) from rest toward the end the stick must be pushed.
#ifndef JOYSTICK_PRESS_FRACTION
#define JOYSTICK_PRESS_FRACTION 0.70
#endif

// Push button (SEL): header pin 29. Pressed = shorted to GND (internal pull-up).
#ifndef JOYSTICK_BUTTON_LINE
#define JOYSTICK_BUTTON_LINE "GPIO5"
#endif

typedef enum {
    JOYSTICK_NONE,
    JOYSTICK_UP,
    JOYSTICK_DOWN,
    JOYSTICK_LEFT,
    JOYSTICK_RIGHT,
} joystick_direction_t;

// Open the ADC and the button, and measure where the stick rests. Returns false (reason printed to stderr) if
// the ADC cannot be opened. A missing button only prints a warning
// (joystick_button_available() then returns false).
bool joystick_init(void);
void joystick_cleanup(void);

// Raw 12-bit ADC value (0..4095) for each axis, or -1 if the read failed.
int joystick_read_raw_x(void);
int joystick_read_raw_y(void);

// Convert a raw ADC value to volts.
double joystick_raw_to_volts(int raw);

// Sample both axes and return the direction being pressed (JOYSTICK_NONE if
// centred or if a read failed). If both axes are pressed, the larger
// deflection wins.
joystick_direction_t joystick_get_direction(void);

// Same as joystick_get_direction() but for readings you already took (e.g. from
// joystick_read_raw_x/y), so a display and the direction come from one sample.
// Returns JOYSTICK_NONE if either value is negative (a failed read).
joystick_direction_t joystick_direction_from_raw(int raw_x, int raw_y);

// Button: 1 = pressed, 0 = released, -1 = not available / read failed.
// Raw level (not debounced).
int joystick_button_pressed(void);
bool joystick_button_available(void);

// The resting values measured at start-up (raw ADC, 0..4095).
void joystick_get_centre(int *centre_x, int *centre_y);

// "UP", "DOWN", "LEFT", "RIGHT" or "none".
const char *joystick_direction_name(joystick_direction_t direction);

#endif
