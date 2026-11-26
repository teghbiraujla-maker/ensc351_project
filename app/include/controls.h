// controls.h
// The events the program reacts to. Every input source (the rotary encoder,
// and the typed debug commands in terminal_input.c) produces these same
// events, so the rest of the program never knows where an input came from.

#ifndef _CONTROLS_H_
#define _CONTROLS_H_

typedef enum {
    CONTROL_NONE,
    // The encoder (or the terminal pretending to be the encoder)
    CONTROL_TURN,          // steps: +N clockwise clicks, -N counter-clockwise
    CONTROL_PRESS,         // short press
    CONTROL_LONG_PRESS,    // held for about 1 second
    // Debug commands (terminal only)
    CONTROL_HELP,
    CONTROL_STATUS,
    CONTROL_LIST,
    CONTROL_QUIT,
    CONTROL_UNKNOWN,       // a typed line that is not a command
} control_type_t;

typedef struct {
    control_type_t type;
    int steps;             // only used by CONTROL_TURN
} control_event_t;

#endif
