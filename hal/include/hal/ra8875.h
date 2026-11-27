// ra8875.h
// Driver for the RA8875 display controller (Adafruit RA8875 board) with the
// 7" 800x480 TFT, over SPI.
//
// Wiring (see README "Wiring"):
//   SCK -> pin 23, MOSI -> pin 19, CS -> pin 26 (CE1 = /dev/spidev0.1),
//   RST -> pin 16 (GPIO23), WAIT -> pin 18 (GPIO24), VIN -> 5 V, GND.
//   MISO is NOT used: boards before "Rev E" do not release MISO, which would
//   corrupt the joystick ADC sharing the bus. The driver never reads from the
//   chip; it waits a safe, calculated time after each drawing command instead.
//
// Drawing uses the RA8875's own hardware (filled rectangles and triangles,
// built-in 8x16 font with 1x-4x enlargement), so each operation is only a
// few bytes of SPI traffic. Every 2-byte command goes in its own SPI transfer.

#ifndef _RA8875_H_
#define _RA8875_H_

#include <stdbool.h>
#include <stdint.h>

#define RA8875_WIDTH  800
#define RA8875_HEIGHT 480

#define RA8875_SPI_DEVICE     "/dev/spidev0.1"
#define RA8875_RESET_LINE     "GPIO23"   // header pin 16
#define RA8875_WAIT_LINE      "GPIO24"   // header pin 18 (low = busy)
#define RA8875_DEFAULT_SPI_HZ 500000     // after start-up; slow = fewer bit errors on breadboard wires
                                         // (see display_test --stress / --spi-hz)

// Reset and set up the display, clear it to black and turn the backlight on.
// spi_hz: clock to use after start-up (0 = RA8875_DEFAULT_SPI_HZ).
// Returns false (reason on stderr) if the SPI device cannot be opened.
bool ra8875_init(uint32_t spi_hz);
void ra8875_cleanup(void);       // turns the display off

// Drawing (colours are RGB565). Off-screen parts are clipped.
void ra8875_fill_rect(int x, int y, int w, int h, uint16_t color);
void ra8875_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);
// Built-in font: each character is 8*(scale+1) x 16*(scale+1) pixels, drawn
// with `bg` behind it. scale 0..3. Text is ISO 8859-1.
void ra8875_text(int x, int y, int scale, uint16_t fg, uint16_t bg, const char *text);

// 0 = off .. 255 = full brightness.
void ra8875_backlight(uint8_t level);

#endif
