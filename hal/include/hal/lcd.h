// lcd.h
// Character LCD, as seen by the rest of the program: a grid of text.
//
// The application builds a whole frame (LCD_ROWS lines of LCD_COLS characters)
// and hands it to lcd_show_frame(). Everything about the real display (pins,
// controller commands, SPI/I2C/GPIO) stays inside lcd.c, so the screen layout
// code in app/src/lcd_ui.c never has to change when the driver does.
//
// The size can be changed at build time: -DLCD_COLS=16 -DLCD_ROWS=2
// (or the cache variables of the same name in the top-level CMakeLists.txt).

#ifndef HAL_LCD_H_
#define HAL_LCD_H_

#include <stdbool.h>

#ifndef LCD_COLS
#define LCD_COLS 20
#endif
#ifndef LCD_ROWS
#define LCD_ROWS 4
#endif

typedef struct {
    // Every row is exactly LCD_COLS printable characters (padded with spaces)
    // followed by a '\0'.
    char row[LCD_ROWS][LCD_COLS + 1];
} lcd_frame_t;

// Start the display. Returns false if it cannot be used (the caller may carry
// on without a screen). Safe to call twice.
bool lcd_init(void);

// Blank/release the display.
void lcd_cleanup(void);

// Show a frame. Must only be called between lcd_init() and lcd_cleanup().
// Callers serialise their calls; the driver does not need to be thread-safe.
void lcd_show_frame(const lcd_frame_t *frame);

#endif
