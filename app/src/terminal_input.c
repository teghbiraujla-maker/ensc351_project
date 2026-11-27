#include "terminal_input.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "app_log.h"

#define LINE_CAPACITY 256
#define MAX_TURN_STEPS 100

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

static bool is_right(char c) { return c == '+' || c == '.' || c == '>'; }
static bool is_left(char c)  { return c == '-' || c == ',' || c == '<'; }

static int clamp_steps(long steps)
{
    if (steps > MAX_TURN_STEPS) return MAX_TURN_STEPS;
    return (int)steps;
}

// "+++" -> 3 clicks right, "--" -> 2 clicks left, "+5" / "-5" -> 5 clicks.
// Returns 0 if the word is not a turn command.
static int parse_turn(const char *word)
{
    char first = word[0];
    if (!is_right(first) && !is_left(first)) {
        return 0;
    }
    int direction = is_right(first) ? 1 : -1;

    if (isdigit((unsigned char)word[1])) {               // "+5"
        char *end = NULL;
        long steps = strtol(word + 1, &end, 10);
        if (*end != '\0' || steps <= 0) {
            return 0;
        }
        return direction * clamp_steps(steps);
    }
    long repeats = 0;                                    // "+++"
    for (const char *c = word; *c != '\0'; c++) {
        if ((direction > 0 && !is_right(*c)) || (direction < 0 && !is_left(*c))) {
            return 0;
        }
        repeats++;
    }
    return direction * clamp_steps(repeats);
}

// "f" -> 1, "f20" -> 20 (letter followed by an optional count). 0 = no match.
static int parse_letter_count(const char *word, char letter)
{
    if (tolower((unsigned char)word[0]) != letter) {
        return 0;
    }
    if (word[1] == '\0') {
        return 1;
    }
    char *end = NULL;
    long count = strtol(word + 1, &end, 10);
    if (*end != '\0' || count <= 0 || !isdigit((unsigned char)word[1])) {
        return 0;
    }
    return clamp_steps(count);
}

int terminal_input_translate(const char *text, control_event_t *out, int max)
{
    while (isspace((unsigned char)*text)) {
        text++;
    }
    char word[LINE_CAPACITY];
    snprintf(word, sizeof(word), "%s", text);
    size_t length = strlen(word);
    while (length > 0 && isspace((unsigned char)word[length - 1])) {
        word[--length] = '\0';
    }
    if (length == 0 || max < 2) {
        return 0;                         // just pressed enter
    }

    control_event_t event = { CONTROL_UNKNOWN, 0 };
    int steps = parse_turn(word);
    int count;
    if (steps != 0) {
        event.type = CONTROL_TURN;
        event.steps = steps;
    } else if (!strcasecmp(word, "p") || !strcasecmp(word, "press")) {
        event.type = CONTROL_PRESS;
    } else if (!strcasecmp(word, "x") || !strcasecmp(word, "hold")) {
        event.type = CONTROL_LONG_PRESS;
    } else if ((count = parse_letter_count(word, 'u')) > 0) {
        event.type = CONTROL_VOLUME;
        event.steps = count;
    } else if ((count = parse_letter_count(word, 'd')) > 0) {
        event.type = CONTROL_VOLUME;
        event.steps = -count;
    } else if ((count = parse_letter_count(word, 'f')) > 0 ||
               (count = -parse_letter_count(word, 'b')) < 0) {
        // Hold the stick right/left for `count` seconds of skipping, then let go.
        out[0].type = CONTROL_SEEK_STEP;
        out[0].steps = count;
        out[1].type = CONTROL_SEEK_RELEASE;
        out[1].steps = 0;
        return 2;
    } else if (!strcasecmp(word, "g") || !strcasecmp(word, "guide")) {
        event.type = CONTROL_GUIDE;
    } else if (!strcasecmp(word, "i") || !strcasecmp(word, "info") || !strcasecmp(word, "status")) {
        event.type = CONTROL_STATUS;
    } else if (!strcasecmp(word, "l") || !strcasecmp(word, "list")) {
        event.type = CONTROL_LIST;
    } else if (!strcasecmp(word, "h") || !strcasecmp(word, "help") || !strcmp(word, "?")) {
        event.type = CONTROL_HELP;
    } else if (!strcasecmp(word, "q") || !strcasecmp(word, "quit") || !strcasecmp(word, "exit")) {
        event.type = CONTROL_QUIT;
    }
    out[0] = event;
    return 1;
}

int terminal_input_fd(void)
{
    return STDIN_FILENO;
}

bool terminal_input_read(terminal_input_deliver_t deliver)
{
    ssize_t got = read(STDIN_FILENO, pending + pending_length, sizeof(pending) - pending_length);
    if (got == 0) {
        return false;                     // EOF: stdin closed
    }
    if (got > 0) {
        pending_length += (size_t)got;
    }

    char line[LINE_CAPACITY];
    while (take_line(line, sizeof(line))) {
        control_event_t events[2];
        int count = terminal_input_translate(line, events, 2);
        if (count > 0) {
            app_log_input(line);          // so the log shows what was typed
        }
        for (int i = 0; i < count; i++) {
            deliver(events[i]);
        }
    }
    return true;
}

const char *terminal_input_help_text(void)
{
    return
        "Typed commands (they pretend to be the encoder and joystick):\n"
        "  Encoder:  +  turn clockwise (+++ or +5 = several clicks)   -  counter-clockwise\n"
        "            p  press (pause/resume, or play the highlighted stop)\n"
        "            x  hold (end the track)\n"
        "  Joystick: u  up = volume +5% (u3 = +15%)    d  down = volume -5%\n"
        "            f  right = skip forward 1 s (f20 = hold for 20 s of skipping)\n"
        "            b  left = skip back 1 s (b20 = 20 s)\n"
        "            g  press = show / close the directions\n"
        "  Debug:    i  status    l  list the stops    h  this help    q  quit\n";
}
