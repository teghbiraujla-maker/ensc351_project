// gpio.h
// Small wrapper around libgpiod v2: open ONE GPIO line by its name
// (e.g. "GPIO17"), wherever it is.
//
// On the BeagleY-AI the header pins are spread over several gpiochips
// (GPIO17 and GPIO27 are on different chips, for example), so every line is
// looked up and requested on its own. Callers never deal with chips.
//
// Built from gpio.c (real hardware) or gpio_stub.c (-DENABLE_GPIO=OFF, where
// every open fails and the program runs without GPIO devices).

#ifndef _HAL_GPIO_H_
#define _HAL_GPIO_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct gpio_line gpio_line_t;

typedef struct {
    bool rising;              // true = went high, false = went low
    uint64_t timestamp_ns;    // kernel timestamp, for putting events from several lines in order
} gpio_edge_t;

// Input line. pull_up: enable the internal pull-up. edge_events: report
// rising and falling edges (read them with gpio_read_edges(); gpio_fd()
// becomes readable when edges are waiting). Returns NULL (reason on stderr)
// if the line does not exist or cannot be requested.
gpio_line_t *gpio_open_input(const char *name, const char *consumer, bool pull_up, bool edge_events);

// Output line, starting at initial_value (0 or 1).
gpio_line_t *gpio_open_output(const char *name, const char *consumer, int initial_value);

void gpio_close(gpio_line_t *line);    // NULL is ignored

// Current level: 1 = high, 0 = low, -1 = error.
int gpio_get(gpio_line_t *line);

bool gpio_set(gpio_line_t *line, int value);

// For poll(): readable when edge events are waiting (edge_events lines only).
int gpio_fd(gpio_line_t *line);

// Read up to max waiting edges (call when gpio_fd() is readable).
// Returns the number read, or -1 on error.
int gpio_read_edges(gpio_line_t *line, gpio_edge_t *out, int max);

#endif
