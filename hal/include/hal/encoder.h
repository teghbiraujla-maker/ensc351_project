// encoder.h
// Rotary encoder with a push button, read through GPIO (libgpiod v2).
//
// A background thread watches the pins and calls the callbacks below. The
// callbacks run ON THAT THREAD, so keep them short and thread-safe (e.g. push
// an event into a queue for the main loop).
//
// Pins (by line name): A = GPIO17 (pin 11), B = GPIO27 (pin 13),
// button = GPIO22 (pin 15, active low, internal pull-up). Change them at the
// top of encoder.c.

#ifndef ENCODER_H_
#define ENCODER_H_

#include <stdbool.h>

typedef enum {
    ENCODER_PRESS_SHORT,   // pressed and released before the long-press time
    ENCODER_PRESS_LONG,    // held for ENCODER_LONG_PRESS_MS (fires while still held)
} Encoder_press_t;

#define ENCODER_LONG_PRESS_MS 1000

// direction: +1 for one click clockwise, -1 for one click counter-clockwise.
typedef void (*Encoder_callback_t)(int direction);
typedef void (*Encoder_button_callback_t)(Encoder_press_t press);

// Returns false (reason printed to stderr) if the encoder pins cannot be
// opened. The program can carry on without the encoder in that case.
// onPress may be NULL. If the button pin is missing, turning still works.
bool Encoder_init(Encoder_callback_t onTurn, Encoder_button_callback_t onPress);

// Safe to call even if Encoder_init() failed or was never called.
void Encoder_cleanup(void);

#endif
