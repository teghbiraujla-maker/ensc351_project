#include "hal/gpio.h"
#include <assert.h>
#include <gpiod.h>     // libgpiod v2 API
#include <stdio.h>
#include <stdlib.h>

#define GPIO_CHIP_PATH_FORMAT    "/dev/gpiochip%d"
#define MAX_GPIO_CHIPS_TO_SEARCH 16
#define EDGE_BUFFER_CAPACITY     64

struct gpio_line {
    struct gpiod_line_request *request;
    struct gpiod_edge_event_buffer *edges;   // NULL unless edge_events
    unsigned int offset;
};

// Find which chip has `name`, and request it with `settings`.
static struct gpiod_line_request *request_by_name(const char *name, const char *consumer,
                                                  struct gpiod_line_settings *settings,
                                                  unsigned int *offset_out, bool *found)
{
    *found = false;
    for (int chip_number = 0; chip_number < MAX_GPIO_CHIPS_TO_SEARCH; chip_number++) {
        char path[32];
        snprintf(path, sizeof(path), GPIO_CHIP_PATH_FORMAT, chip_number);
        struct gpiod_chip *chip = gpiod_chip_open(path);
        if (chip == NULL) {
            continue;
        }
        int offset = gpiod_chip_get_line_offset_from_name(chip, name);
        if (offset < 0) {
            gpiod_chip_close(chip);
            continue;
        }
        *found = true;
        *offset_out = (unsigned int)offset;

        struct gpiod_line_config *line_config = gpiod_line_config_new();
        struct gpiod_request_config *request_config = gpiod_request_config_new();
        assert(line_config != NULL && request_config != NULL);
        gpiod_line_config_add_line_settings(line_config, offset_out, 1, settings);
        gpiod_request_config_set_consumer(request_config, consumer);
        gpiod_request_config_set_event_buffer_size(request_config, EDGE_BUFFER_CAPACITY);

        struct gpiod_line_request *request = gpiod_chip_request_lines(chip, request_config, line_config);
        gpiod_request_config_free(request_config);
        gpiod_line_config_free(line_config);
        gpiod_chip_close(chip);            // the request stays valid on its own
        return request;
    }
    return NULL;
}

static gpio_line_t *open_line(const char *name, const char *consumer,
                              struct gpiod_line_settings *settings, bool edge_events)
{
    unsigned int offset = 0;
    bool found = false;
    struct gpiod_line_request *request = request_by_name(name, consumer, settings, &offset, &found);
    if (request == NULL) {
        if (!found) {
            fprintf(stderr, "gpio: line %s not found on any gpiochip\n", name);
        } else {
            perror("gpio: cannot request line (in use, or not running with sudo?)");
        }
        return NULL;
    }

    gpio_line_t *line = calloc(1, sizeof(*line));
    assert(line != NULL);
    line->request = request;
    line->offset = offset;
    if (edge_events) {
        line->edges = gpiod_edge_event_buffer_new(EDGE_BUFFER_CAPACITY);
        if (line->edges == NULL) {
            gpio_close(line);
            return NULL;
        }
    }
    return line;
}

gpio_line_t *gpio_open_input(const char *name, const char *consumer, bool pull_up, bool edge_events)
{
    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    assert(settings != NULL);
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    if (edge_events) {
        gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_BOTH);
    }
    if (pull_up) {
        gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_UP);
    }
    gpio_line_t *line = open_line(name, consumer, settings, edge_events);
    if (line == NULL && pull_up) {
        // Some lines do not support bias settings: retry without the pull-up
        // (an external pull-up resistor is then needed).
        gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_AS_IS);
        line = open_line(name, consumer, settings, edge_events);
        if (line != NULL) {
            fprintf(stderr, "gpio: %s opened WITHOUT internal pull-up\n", name);
        }
    }
    gpiod_line_settings_free(settings);
    return line;
}

gpio_line_t *gpio_open_output(const char *name, const char *consumer, int initial_value)
{
    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    assert(settings != NULL);
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_output_value(settings,
        initial_value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
    gpio_line_t *line = open_line(name, consumer, settings, false);
    gpiod_line_settings_free(settings);
    return line;
}

void gpio_close(gpio_line_t *line)
{
    if (line == NULL) {
        return;
    }
    if (line->edges != NULL) {
        gpiod_edge_event_buffer_free(line->edges);
    }
    if (line->request != NULL) {
        gpiod_line_request_release(line->request);
    }
    free(line);
}

int gpio_get(gpio_line_t *line)
{
    enum gpiod_line_value value = gpiod_line_request_get_value(line->request, line->offset);
    if (value == GPIOD_LINE_VALUE_ERROR) {
        return -1;
    }
    return (value == GPIOD_LINE_VALUE_ACTIVE) ? 1 : 0;
}

bool gpio_set(gpio_line_t *line, int value)
{
    return gpiod_line_request_set_value(line->request, line->offset,
        value ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE) == 0;
}

int gpio_fd(gpio_line_t *line)
{
    return gpiod_line_request_get_fd(line->request);
}

int gpio_read_edges(gpio_line_t *line, gpio_edge_t *out, int max)
{
    if (line->edges == NULL || max <= 0) {
        return 0;
    }
    if (max > EDGE_BUFFER_CAPACITY) {
        max = EDGE_BUFFER_CAPACITY;
    }
    int count = gpiod_line_request_read_edge_events(line->request, line->edges, (size_t)max);
    if (count < 0) {
        return -1;
    }
    for (int i = 0; i < count; i++) {
        struct gpiod_edge_event *event = gpiod_edge_event_buffer_get_event(line->edges, (unsigned long)i);
        out[i].rising = (gpiod_edge_event_get_event_type(event) == GPIOD_EDGE_EVENT_RISING_EDGE);
        out[i].timestamp_ns = gpiod_edge_event_get_timestamp_ns(event);
    }
    return count;
}
