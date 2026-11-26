// terminal_input.h
// Temporary input source: the user types commands in the terminal.
//
// The rest of the program only deals with input_event_t, never with raw text,
// so a keypad (or the joystick) can later replace this module by producing
// the same events, without changing main.c's logic.

#ifndef _TERMINAL_INPUT_H_
#define _TERMINAL_INPUT_H_

typedef enum {
    INPUT_NONE,            // nothing happened (timeout)
    INPUT_SELECT_TRACK,    // value = the track number typed (1-based)
    INPUT_TOGGLE_PAUSE,
    INPUT_STOP,
    INPUT_VOLUME_UP,
    INPUT_VOLUME_DOWN,
    INPUT_SEEK_FORWARD,
    INPUT_SEEK_BACK,
    INPUT_CURSOR_UP,       // move the highlighted stop up (encoder turned back)
    INPUT_CURSOR_DOWN,     // move the highlighted stop down (encoder turned forward)
    INPUT_CONFIRM,         // play the highlighted stop (encoder press)
    INPUT_LIST,
    INPUT_STATUS,
    INPUT_HELP,
    INPUT_QUIT,            // quit command, or the terminal was closed (EOF)
    INPUT_UNKNOWN,
} input_type_t;

typedef struct {
    input_type_t type;
    int value;             // only used by INPUT_SELECT_TRACK
} input_event_t;

// Wait up to timeout_ms for the user to enter a line, and translate it.
// Returns INPUT_NONE if the time runs out (or a signal interrupts the wait).
input_event_t terminal_input_poll(int timeout_ms);

// Text describing the commands, for the help screen.
const char *terminal_input_help_text(void);

#endif
