// display_test - check the RA8875 screen wiring before running the tour.
//
//   sudo ./display_test                    test pattern at the default SPI speed
//   sudo ./display_test --spi-hz 250000    try a slower (or faster) SPI clock
//   sudo ./display_test --stress           repeat the tour's kind of drawing (bar, list
//                                          rows, triangle, text) to show up glitches
//
// You should see: eight colour bars, a white triangle, four sizes of text and
// a timing line. If the screen stays dark or shows noise, see the README
// ("Screen troubleshooting"). Runs until Ctrl-C (or --seconds N).

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "canvas.h"
#include "hal/ra8875.h"

static volatile sig_atomic_t stop_requested = 0;
static void handle_signal(int signal_number) { (void)signal_number; stop_requested = 1; }

static double now_sec(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

// Draws, over and over, the same kinds of updates the tour does. Everything
// has a fixed place and colour, so ANY stray colour, streak or misplaced
// text is a corrupted command (usually SPI bit errors on the wires).
static void stress(void)
{
    const uint16_t black = 0x0000, white = CANVAS_RGB(255, 255, 255);
    const uint16_t red = CANVAS_RGB(166, 25, 46), grey = CANVAS_RGB(60, 68, 86);
    const uint16_t amber = CANVAS_RGB(245, 180, 40);
    ra8875_fill_rect(0, 0, 800, 480, black);
    ra8875_text(10, 8, 0, white, black, "STRESS TEST: any stray colour, streak or misplaced text = corrupted commands");
    ra8875_text(10, 26, 0, white, black, "Only these should change: the red bar, the highlighted row, the counter.");
    long rounds = 0, commands = 0;
    int highlight = 0;
    while (!stop_requested) {
        ra8875_fill_rect(20, 400, 760, 30, grey);
        commands++;
        for (int x = 0; x < 760 && !stop_requested; x += 2) {
            ra8875_fill_rect(20 + x, 400, 2, 30, red);             // the bar growing, like playback
            commands++;
            if (x % 76 == 0) {                                     // ten times per pass: rows + text
                for (int row = 0; row < 4; row++) {
                    int y = 60 + row * 70;
                    bool on = (row == highlight);
                    uint16_t bg = on ? red : black;
                    ra8875_fill_rect(404, y, 386, 46, bg);
                    ra8875_fill_triangle(412, y + 11, 412, y + 35, 436, y + 23, amber);
                    char label[32];
                    snprintf(label, sizeof(label), "Row %d", row + 1);
                    ra8875_text(452, y + 7, 1, white, bg, label);
                    commands += 3;
                }
                highlight = (highlight + 1) % 4;
                char counter[64];
                snprintf(counter, sizeof(counter), "pass %ld  commands %ld  ", rounds + 1, commands);
                ra8875_text(24, 440, 1, white, black, counter);
                commands++;
            }
        }
        rounds++;
        printf("\rpass %ld done, %ld drawing commands so far", rounds, commands);
        fflush(stdout);
    }
    printf("\n");
}

int main(int argc, char *argv[])
{
    unsigned long spi_hz = RA8875_DEFAULT_SPI_HZ;
    long seconds = -1;
    bool stress_mode = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--spi-hz") && i + 1 < argc) {
            spi_hz = strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--stress")) {
            stress_mode = true;
        } else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) {
            seconds = strtol(argv[++i], NULL, 10);
        } else {
            printf("Usage: %s [--spi-hz HZ] [--stress] [--seconds N]\n", argv[0]);
            return !strcmp(argv[i], "--help") ? 0 : 1;
        }
    }

    printf("Display test: %s, SPI clock %lu Hz after start-up.\n", RA8875_SPI_DEVICE, spi_hz);
    if (!ra8875_init((uint32_t)spi_hz)) {
        return 1;
    }
    printf("Start-up sequence sent. Drawing the test pattern...\n");

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    if (stress_mode) {
        printf("Stress test - watch the screen for glitches. Ctrl-C to finish.\n");
        stress();
        ra8875_cleanup();
        printf("Display off. Done.\n");
        return 0;
    }

    // Colour bars across the top
    const uint16_t bars[8] = {
        CANVAS_RGB(255, 255, 255), CANVAS_RGB(255, 255, 0), CANVAS_RGB(0, 255, 255),
        CANVAS_RGB(0, 255, 0), CANVAS_RGB(255, 0, 255), CANVAS_RGB(255, 0, 0),
        CANVAS_RGB(0, 0, 255), CANVAS_RGB(166, 25, 46),
    };
    double start = now_sec();
    for (int i = 0; i < 8; i++) {
        ra8875_fill_rect(i * 100, 0, 100, 120, bars[i]);
    }
    double bars_time = now_sec() - start;

    ra8875_fill_triangle(680, 150, 680, 250, 770, 200, CANVAS_RGB(255, 255, 255));

    uint16_t white = CANVAS_RGB(255, 255, 255);
    uint16_t black = 0x0000;
    start = now_sec();
    ra8875_text(10, 140, 0, white, black, "Scale 0: the quick brown fox (8x16)");
    ra8875_text(10, 164, 1, white, black, "Scale 1: quick brown fox");
    ra8875_text(10, 204, 2, white, black, "Scale 2: brown fox");
    ra8875_text(10, 260, 3, CANVAS_RGB(245, 180, 40), black, "Scale 3: fox");
    double text_time = now_sec() - start;

    char line[96];
    snprintf(line, sizeof(line), "SPI %lu Hz: 8 bars %.0f ms, 4 text lines %.0f ms",
             spi_hz, bars_time * 1000.0, text_time * 1000.0);
    ra8875_text(10, 350, 0, CANVAS_RGB(150, 160, 178), black, line);
    ra8875_text(10, 380, 1, white, black, "If you can read this, the screen works!");
    printf("%s\n", line);
    printf("Check the screen: 8 colour bars (white, yellow, cyan, green, magenta, red, blue, SFU red),\n");
    printf("a white triangle on the right, and 4 sizes of text. Ctrl-C to finish.\n");

    double end = now_sec() + (double)seconds;
    while (!stop_requested && (seconds < 0 || now_sec() < end)) {
        struct timespec tick = { 0, 100000000L };   // 0.1 s
        nanosleep(&tick, NULL);
    }
    ra8875_cleanup();
    printf("Display off. Done.\n");
    return 0;
}
