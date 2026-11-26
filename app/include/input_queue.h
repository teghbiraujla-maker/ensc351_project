// input_queue.h
// Thread-safe queue of control events.
//
// The encoder thread pushes events; the main loop pops them. The queue also
// has a file descriptor that becomes readable whenever events are waiting, so
// the main loop can wait for "encoder event OR typed input OR timeout" with a
// single poll() call.

#ifndef _INPUT_QUEUE_H_
#define _INPUT_QUEUE_H_

#include <stdbool.h>
#include "controls.h"

#define INPUT_QUEUE_CAPACITY 64

bool input_queue_init(void);
void input_queue_cleanup(void);

// Safe to call from any thread. Returns false (event dropped) if the queue is full.
bool input_queue_push(control_event_t event);

// Take the oldest event. Returns false if the queue is empty.
bool input_queue_pop(control_event_t *event);

// Readable (POLLIN) while the queue holds events.
int input_queue_fd(void);

#endif
