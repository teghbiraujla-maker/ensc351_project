// joystick.h
// HAL module for the analog joystick (X and Y axes read through the SPI ADC).
//
// The module is self-calibrating: each axis's min/max range starts as a
// conservative guess and widens as the stick is moved, so "pressed" always
// means "most of the way to the extreme seen so far".

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

typedef enum {
    JOYSTICK_NONE,
    JOYSTICK_UP,
    JOYSTICK_DOWN,
    JOYSTICK_LEFT,
    JOYSTICK_RIGHT,
} joystick_direction_t;

// Open the ADC. Returns false (reason printed to stderr) on failure.
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

// "UP", "DOWN", "LEFT", "RIGHT" or "none".
const char *joystick_direction_name(joystick_direction_t direction);

#endif
