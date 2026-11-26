// lcd.c - LCD HAL.
//
// STATUS: placeholder back end. There is no hardware driver in here yet, so
// lcd_show_frame() does not touch any pins. For development without the
// screen it can print every frame to stderr as a box:
//
//     TOUR_LCD_MIRROR=1 scripts/run_on_board.sh tour_player
//
// To support the real display, replace the bodies of the three functions below
// (initialise the controller in lcd_init, write the LCD_ROWS lines of the frame
// in lcd_show_frame, blank it in lcd_cleanup). Nothing else in the project
// needs to change.

#include "hal/lcd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool s_isInitialized = false;
static bool s_mirrorToStderr = false;

bool lcd_init(void)
{
    if (s_isInitialized) {
        return true;
    }
    const char *mirror = getenv("TOUR_LCD_MIRROR");
    s_mirrorToStderr = (mirror != NULL && mirror[0] != '\0' && strcmp(mirror, "0") != 0);
    s_isInitialized = true;
    return true;
}

void lcd_cleanup(void)
{
    s_isInitialized = false;
}

static void print_border(void)
{
    fputc('+', stderr);
    for (int col = 0; col < LCD_COLS; col++) {
        fputc('-', stderr);
    }
    fputs("+\n", stderr);
}

void lcd_show_frame(const lcd_frame_t *frame)
{
    if (!s_isInitialized || frame == NULL || !s_mirrorToStderr) {
        return;
    }
    print_border();
    for (int row = 0; row < LCD_ROWS; row++) {
        fprintf(stderr, "|%s|\n", frame->row[row]);
    }
    print_border();
}
