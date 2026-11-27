// joystick_input.h
// Turns the joystick into control events (see controls.h):
//
//   Stick UP / DOWN    CONTROL_VOLUME +1 / -1 at once, then every 0.5 s while held
//   Stick RIGHT / LEFT CONTROL_SEEK_STEP +1 / -1 (1 s) at once, then every 0.1 s
//                      (= 10 s of skipping per second held); CONTROL_SEEK_RELEASE
//                      when the stick leaves that direction
//   Stick pressed      CONTROL_GUIDE (debounced; once per press)
//
// A background thread samples the joystick every JOYSTICK_SAMPLE_MS and passes
// each event to the callback given to joystick_input_start(). The decision
// logic is a separate pure function (joystick_logic_update) so it can be
// unit tested without hardware.

#ifndef _JOYSTICK_INPUT_H_
#define _JOYSTICK_INPUT_H_

#include <stdbool.h>
#include "controls.h"
#include "hal/joystick.h"
#include "hal/press_detector.h"

#define JOYSTICK_SAMPLE_MS        10
#define JOYSTICK_VOLUME_REPEAT_MS 500
#define JOYSTICK_SEEK_REPEAT_MS   100
#define JOYSTICK_BUTTON_DEBOUNCE_MS 20

typedef void (*joystick_input_deliver_t)(control_event_t event);

// Opens the joystick and starts the sampling thread. Returns false (and
// prints why) if the joystick's ADC cannot be opened.
bool joystick_input_start(joystick_input_deliver_t deliver);
void joystick_input_stop(void);     // safe to call even if start failed

// ---- Pure logic (used by the thread; exposed for the unit test) ----
typedef struct {
    joystick_direction_t held;      // direction currently held
    long long next_repeat_ms;       // when the held direction fires again
    press_detector_t button;
} joystick_logic_t;

void joystick_logic_init(joystick_logic_t *logic);

// One sample. button: 1 pressed, 0 released, -1 unavailable.
// Writes up to `max` events into `out`; returns how many.
int joystick_logic_update(joystick_logic_t *logic, joystick_direction_t direction,
                          int button, long long now_ms, control_event_t *out, int max);

#endif
