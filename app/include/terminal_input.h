// terminal_input.h
// Debug input: typed commands (over SSH) that pretend to be the encoder, so
// the whole control scheme can be tested without the encoder wired up.
//
//   +   or  .     one click clockwise        (+++ = 3 clicks, +5 = 5 clicks)
//   -   or  ,     one click counter-clockwise (--- = 3 clicks, -5 = 5 clicks)
//   p             short press
//   x   or  hold  long press (hold ~1 s)
//   i / l / h / q status, track list, help, quit
//
// Each typed line becomes one control_event_t (see controls.h), exactly like
// an event from the encoder.

#ifndef _TERMINAL_INPUT_H_
#define _TERMINAL_INPUT_H_

#include <stdbool.h>
#include "controls.h"

// The file descriptor to watch with poll() (standard input).
int terminal_input_fd(void);

// Call when terminal_input_fd() is readable. Reads the waiting text and
// calls deliver() once for each complete command line. Returns false when
// standard input has closed (no more typed input will come).
typedef void (*terminal_input_deliver_t)(control_event_t event);
bool terminal_input_read(terminal_input_deliver_t deliver);

// Translate one typed line (exposed for the unit test).
control_event_t terminal_input_translate(const char *line);

// Text describing the commands, for the help screen.
const char *terminal_input_help_text(void);

#endif
