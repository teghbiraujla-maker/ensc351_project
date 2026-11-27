// controls.h
// The events the program reacts to. Every input source (the rotary encoder,
// the joystick, and the typed debug commands in terminal_input.c) produces
// these same events, so the rest of the program never knows where an input
// came from.

#ifndef _CONTROLS_H_
#define _CONTROLS_H_

typedef enum {
    CONTROL_NONE,
    // Rotary encoder
    CONTROL_TURN,          // steps: +N clockwise clicks, -N counter-clockwise
    CONTROL_PRESS,         // short press
    CONTROL_LONG_PRESS,    // held for about 1 second
    // Joystick
    CONTROL_VOLUME,        // steps: +1 = volume up one step, -1 = down
    CONTROL_SEEK_STEP,     // steps: +1 = 1 s forward, -1 = 1 s back (while held)
    CONTROL_SEEK_RELEASE,  // the stick went back to centre: jump to the chosen spot
    CONTROL_GUIDE,         // stick pressed: show / close the directions
    // Debug commands (terminal only)
    CONTROL_HELP,
    CONTROL_STATUS,
    CONTROL_LIST,
    CONTROL_QUIT,
    CONTROL_UNKNOWN,       // a typed line that is not a command
} control_type_t;

typedef struct {
    control_type_t type;
    int steps;             // used by TURN, VOLUME and SEEK_STEP
} control_event_t;

#endif
