// terminal_input.h
// Debug input: typed commands (over SSH) that pretend to be the encoder and
// the joystick, so the whole control scheme can be tested without them.
//
//   Encoder:  +  /  -       turn one click (+++ or +5 = several clicks)
//             p             press          x  hold (long press)
//   Joystick: u  /  d       volume up / down one step (u3 = three steps)
//             f  /  b       skip forward / back 1 s, then let go (f20 = 20 s)
//             g             press (show / close the directions)
//   Debug:    i  l  h  q    status, list, help, quit
//
// Each typed line becomes one or two control_event_t (see controls.h),
// exactly like events from the real devices.

#ifndef _TERMINAL_INPUT_H_
#define _TERMINAL_INPUT_H_

#include <stdbool.h>
#include "controls.h"

// The file descriptor to watch with poll() (standard input).
int terminal_input_fd(void);

// Call when terminal_input_fd() is readable. Reads the waiting text and
// calls deliver() for each event. Returns false when standard input has
// closed (no more typed input will come).
typedef void (*terminal_input_deliver_t)(control_event_t event);
bool terminal_input_read(terminal_input_deliver_t deliver);

// Translate one typed line into up to `max` events (max >= 2); returns how
// many (0 for an empty line). Exposed for the unit test.
int terminal_input_translate(const char *line, control_event_t *out, int max);

// Text describing the commands, for the help screen.
const char *terminal_input_help_text(void);

#endif
