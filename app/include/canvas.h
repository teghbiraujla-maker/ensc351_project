// canvas.h
// The few drawing operations the GUI needs. The real screen (RA8875) does
// each of these in hardware from a short command, which keeps SPI traffic
// tiny. gui.c only draws through this interface, so it can also draw into a
// recorder (tools/gui_preview) to make preview images on a laptop.
//
// Text uses the display's built-in 8x16 font (ISO 8859-1). scale 0..3
// enlarges it: each character is CANVAS_CHAR_W(scale) x CANVAS_CHAR_H(scale)
// pixels, and the background colour fills each character cell.

#ifndef _CANVAS_H_
#define _CANVAS_H_

#include <stdint.h>

#define CANVAS_CHAR_W(scale) (8 * ((scale) + 1))
#define CANVAS_CHAR_H(scale) (16 * ((scale) + 1))

// 16-bit colour (RGB565) from 8-bit red, green, blue.
#define CANVAS_RGB(r, g, b) \
    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))

typedef struct {
    int width;
    int height;
    void (*fill_rect)(int x, int y, int w, int h, uint16_t color);
    void (*fill_triangle)(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);
    void (*text)(int x, int y, int scale, uint16_t fg, uint16_t bg, const char *text);
} canvas_t;

#endif
