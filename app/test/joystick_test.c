// joystick_test - live display of the joystick, to confirm the wiring and the
// direction detection.
//
//   sudo ./joystick_test                 run until Ctrl-C
//   sudo ./joystick_test --samples 50    print 50 readings, then a summary
//
// Move the stick around: the raw ADC values (0-4095), the voltage, a bar showing
// where the stick is, and the direction the HAL detects are shown ~10 times a
// second. If UP/DOWN or LEFT/RIGHT are the wrong way round, see the README
// ("Joystick up/down or left/right reversed").

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "hal/joystick.h"
#include "hal/spi_adc.h"

#define BAR_WIDTH        21
#define SAMPLE_PERIOD_MS 100

static volatile sig_atomic_t stop_requested = 0;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static void sleep_ms(int milliseconds)
{
    struct timespec delay = { milliseconds / 1000, (long)(milliseconds % 1000) * 1000000L };
    nanosleep(&delay, NULL);
}

// "[----------#----------]" with the # showing where raw (0..4095) sits.
static void make_bar(int raw, char *out)
{
    int position = (raw * (BAR_WIDTH - 1) + SPI_ADC_MAX_VALUE / 2) / SPI_ADC_MAX_VALUE;
    out[0] = '[';
    for (int i = 0; i < BAR_WIDTH; i++) {
        out[i + 1] = (i == position) ? '#' : (i == BAR_WIDTH / 2 ? '|' : '-');
    }
    out[BAR_WIDTH + 1] = ']';
    out[BAR_WIDTH + 2] = '\0';
}

int main(int argc, char *argv[])
{
    long max_samples = -1;     // -1 = run until Ctrl-C
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--samples") && i + 1 < argc) {
            max_samples = strtol(argv[++i], NULL, 10);
        } else {
            printf("Usage: %s [--samples N]\n", argv[0]);
            return !strcmp(argv[i], "--help") ? 0 : 1;
        }
    }

    if (!joystick_init()) {
        return 1;
    }

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    bool interactive = isatty(STDOUT_FILENO);
    int min_x = SPI_ADC_MAX_VALUE, max_x = 0, min_y = SPI_ADC_MAX_VALUE, max_y = 0;
    long samples = 0;
    long read_errors = 0;

    printf("Joystick test - move the stick; press Ctrl-C to quit.\n");
    printf("Leave the stick alone for a moment at start. Press the stick: SEL shows PRESSED.\n");
    int centre_x, centre_y;
    joystick_get_centre(&centre_x, &centre_y);
    printf("Resting position measured at start: X %d, Y %d. A direction counts once the stick\n"
           "is pushed %.0f%% of the way from there to the end.\n", centre_x, centre_y,
           JOYSTICK_PRESS_FRACTION * 100.0);
    if (!joystick_button_available()) {
        printf("(Button on %s not available - check the SEL wire to header pin 29.)\n", JOYSTICK_BUTTON_LINE);
    }
    printf("\n");

    while (!stop_requested && (max_samples < 0 || samples < max_samples)) {
        int raw_x = joystick_read_raw_x();
        int raw_y = joystick_read_raw_y();
        if (raw_x < 0 || raw_y < 0) {
            read_errors++;
            sleep_ms(SAMPLE_PERIOD_MS);
            continue;
        }
        joystick_direction_t direction = joystick_direction_from_raw(raw_x, raw_y);   // same sample as shown

        if (raw_x < min_x) min_x = raw_x;
        if (raw_x > max_x) max_x = raw_x;
        if (raw_y < min_y) min_y = raw_y;
        if (raw_y > max_y) max_y = raw_y;
        samples++;

        char bar_x[BAR_WIDTH + 3];
        char bar_y[BAR_WIDTH + 3];
        make_bar(raw_x, bar_x);
        make_bar(raw_y, bar_y);
        int button = joystick_button_pressed();
        printf("%sX %4d %.3fV %s  Y %4d %.3fV %s  -> %-5s  SEL %-8s%s",
               interactive ? "\r" : "",
               raw_x, joystick_raw_to_volts(raw_x), bar_x,
               raw_y, joystick_raw_to_volts(raw_y), bar_y,
               joystick_direction_name(direction),
               button < 0 ? "n/a" : (button ? "PRESSED" : "-"),
               interactive ? "" : "\n");
        fflush(stdout);
        sleep_ms(SAMPLE_PERIOD_MS);
    }

    printf("\n\nSummary: %ld readings, %ld read errors\n", samples, read_errors);
    if (samples > 0) {
        printf("  X range seen: %d - %d\n", min_x, max_x);
        printf("  Y range seen: %d - %d\n", min_y, max_y);
    }

    joystick_cleanup();
    return (samples > 0) ? 0 : 1;
}
