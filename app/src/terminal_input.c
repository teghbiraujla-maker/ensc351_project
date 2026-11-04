#include "terminal_input.h"
#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define LINE_CAPACITY 256

// Bytes read from stdin but not yet turned into a line. We read with read()
// rather than fgets() so that poll() and the buffering always agree.
static char pending[LINE_CAPACITY * 4];
static size_t pending_length = 0;

static bool take_line(char *line, size_t line_size)
{
    char *newline = memchr(pending, '\n', pending_length);
    if (newline == NULL) {
        if (pending_length < sizeof(pending)) {
            return false;                 // wait for more input
        }
        newline = pending + pending_length - 1;   // buffer full: force a line break
    }
    size_t length = (size_t)(newline - pending);
    size_t copy_length = (length < line_size - 1) ? length : line_size - 1;
    memcpy(line, pending, copy_length);
    line[copy_length] = '\0';

    size_t consumed = length + 1;
    memmove(pending, pending + consumed, pending_length - consumed);
    pending_length -= consumed;
    return true;
}

static input_event_t translate(const char *text)
{
    input_event_t event = { INPUT_UNKNOWN, 0 };

    while (isspace((unsigned char)*text)) {
        text++;
    }
    char word[LINE_CAPACITY];
    snprintf(word, sizeof(word), "%s", text);
    size_t length = strlen(word);
    while (length > 0 && isspace((unsigned char)word[length - 1])) {
        word[--length] = '\0';
    }
    if (length == 0) {
        event.type = INPUT_NONE;          // just pressed enter
        return event;
    }

    char *end = NULL;
    long number = strtol(word, &end, 10);
    if (*end == '\0' && number > 0 && number < 100000) {
        event.type = INPUT_SELECT_TRACK;
        event.value = (int)number;
        return event;
    }

    if (!strcasecmp(word, "p") || !strcasecmp(word, "pause") || !strcasecmp(word, "resume")) {
        event.type = INPUT_TOGGLE_PAUSE;
    } else if (!strcasecmp(word, "s") || !strcasecmp(word, "stop")) {
        event.type = INPUT_STOP;
    } else if (!strcmp(word, "+") || !strcasecmp(word, "up")) {
        event.type = INPUT_VOLUME_UP;
    } else if (!strcmp(word, "-") || !strcasecmp(word, "down")) {
        event.type = INPUT_VOLUME_DOWN;
    } else if (!strcasecmp(word, "f") || !strcasecmp(word, "forward")) {
        event.type = INPUT_SEEK_FORWARD;
    } else if (!strcasecmp(word, "b") || !strcasecmp(word, "back")) {
        event.type = INPUT_SEEK_BACK;
    } else if (!strcasecmp(word, "l") || !strcasecmp(word, "list")) {
        event.type = INPUT_LIST;
    } else if (!strcasecmp(word, "i") || !strcasecmp(word, "info") || !strcasecmp(word, "status")) {
        event.type = INPUT_STATUS;
    } else if (!strcasecmp(word, "h") || !strcasecmp(word, "help") || !strcmp(word, "?")) {
        event.type = INPUT_HELP;
    } else if (!strcasecmp(word, "q") || !strcasecmp(word, "quit") || !strcasecmp(word, "exit")) {
        event.type = INPUT_QUIT;
    }
    return event;
}

input_event_t terminal_input_poll(int timeout_ms)
{
    input_event_t none = { INPUT_NONE, 0 };
    char line[LINE_CAPACITY];

    // A previous read may have delivered several lines at once.
    if (take_line(line, sizeof(line))) {
        return translate(line);
    }

    struct pollfd watch = { STDIN_FILENO, POLLIN, 0 };
    int ready = poll(&watch, 1, timeout_ms);
    if (ready < 0) {
        return none;                      // interrupted by a signal (e.g. Ctrl-C)
    }
    if (ready == 0) {
        return none;
    }

    ssize_t got = read(STDIN_FILENO, pending + pending_length, sizeof(pending) - pending_length);
    if (got == 0) {
        input_event_t quit = { INPUT_QUIT, 0 };   // EOF: terminal closed
        return quit;
    }
    if (got < 0) {
        return none;
    }
    pending_length += (size_t)got;

    if (take_line(line, sizeof(line))) {
        return translate(line);
    }
    return none;
}

const char *terminal_input_help_text(void)
{
    return
        "Commands:\n"
        "  <number>      play that track (see the list)\n"
        "  p             pause / resume\n"
        "  s             stop\n"
        "  + / -         volume up / down\n"
        "  f / b         skip forward / back 10 seconds\n"
        "  l             list the tracks\n"
        "  i             show what is playing\n"
        "  h             this help\n"
        "  q             quit\n";
}
