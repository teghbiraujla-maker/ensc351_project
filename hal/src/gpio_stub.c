// Used instead of gpio.c when built with -DENABLE_GPIO=OFF (no libgpiod).
// Every open fails, so the encoder, joystick button and screen reset are
// unavailable and the program runs with typed commands only.
#include "hal/gpio.h"
#include <stdio.h>

gpio_line_t *gpio_open_input(const char *name, const char *consumer, bool pull_up, bool edge_events)
{
    (void)consumer;
    (void)pull_up;
    (void)edge_events;
    fprintf(stderr, "gpio: %s unavailable (built with ENABLE_GPIO=OFF)\n", name);
    return NULL;
}

gpio_line_t *gpio_open_output(const char *name, const char *consumer, int initial_value)
{
    (void)consumer;
    (void)initial_value;
    fprintf(stderr, "gpio: %s unavailable (built with ENABLE_GPIO=OFF)\n", name);
    return NULL;
}

void gpio_close(gpio_line_t *line) { (void)line; }
int gpio_get(gpio_line_t *line) { (void)line; return -1; }
bool gpio_set(gpio_line_t *line, int value) { (void)line; (void)value; return false; }
int gpio_fd(gpio_line_t *line) { (void)line; return -1; }
int gpio_read_edges(gpio_line_t *line, gpio_edge_t *out, int max) { (void)line; (void)out; (void)max; return -1; }
