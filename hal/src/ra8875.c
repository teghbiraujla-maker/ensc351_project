#include "hal/ra8875.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "hal/gpio.h"
#include "hal/spi_bus.h"

// SPI framing: every byte to the chip is preceded by a "what is this" byte.
#define RA8875_CMD_WRITE  0x80     // next byte selects a register
#define RA8875_DATA_WRITE 0x00     // next byte is written to the selected register

// Registers used (names from the RA8875 datasheet)
#define REG_PWRR   0x01   // power and display control
#define REG_MRWC   0x02   // memory read/write (text characters go here)
#define REG_PCSR   0x04   // pixel clock
#define REG_SYSR   0x10   // colour depth / interface
#define REG_HDWR   0x14   // horizontal display width
#define REG_HNDFTR 0x15
#define REG_HNDR   0x16
#define REG_HSTR   0x17
#define REG_HPWR   0x18
#define REG_VDHR0  0x19   // vertical display height
#define REG_VDHR1  0x1A
#define REG_VNDR0  0x1B
#define REG_VNDR1  0x1C
#define REG_VSTR0  0x1D
#define REG_VSTR1  0x1E
#define REG_VPWR   0x1F
#define REG_FNCR0  0x21   // font source
#define REG_FNCR1  0x22   // font size / transparency
#define REG_FCURXL 0x2A   // text cursor
#define REG_FCURXH 0x2B
#define REG_FCURYL 0x2C
#define REG_FCURYH 0x2D
#define REG_HSAW0  0x30   // active window
#define REG_HSAW1  0x31
#define REG_VSAW0  0x32
#define REG_VSAW1  0x33
#define REG_HEAW0  0x34
#define REG_HEAW1  0x35
#define REG_VEAW0  0x36
#define REG_VEAW1  0x37
#define REG_MWCR0  0x40   // text / graphics mode
#define REG_BGCR0  0x60   // background colour R, G, B
#define REG_FGCR0  0x63   // foreground colour R, G, B
#define REG_PLLC1  0x88
#define REG_PLLC2  0x89
#define REG_P1CR   0x8A   // PWM1 (backlight) control
#define REG_P1DCR  0x8B   // PWM1 duty cycle
#define REG_MCLR   0x8E   // memory clear
#define REG_DCR    0x90   // draw control
#define REG_DLHSR0 0x91   // shape start x, y; end x, y
#define REG_DTPH0  0xA9   // triangle third point
#define REG_GPIOX  0xC7   // display enable on the Adafruit board

#define DCR_START_FILLED_RECT     0xB0
#define DCR_START_FILLED_TRIANGLE 0xA1

// The RA8875 needs a slow clock until its PLL is running.
#define INIT_SPI_HZ          125000
// After each drawing command we wait for the drawing engine instead of
// reading its busy flag (MISO is not connected). Generous on purpose:
// a fixed 300 us plus 5 pixels per microsecond.
#define DRAW_BASE_DELAY_US   300
#define FILL_PIXELS_PER_US   5
// Characters: the bigger the enlargement, the longer the chip needs.
#define TEXT_CHAR_DELAY_SMALL_US 500     // scale 0
#define TEXT_CHAR_DELAY_US   1000        // scale 1
#define TEXT_CHAR_DELAY_BIG_US 1500      // scale 2 and 3
#define MAX_KERNEL_DELAY_US  60000     // longest wait stored with a frame (delay_usecs is 16 bits)
#define WAIT_PIN_TIMEOUT_MS  50

#define BATCH_MAX 1024                 // frames queued before sending (one whole command fits)

static int spi_fd = -1;
static uint32_t current_hz = INIT_SPI_HZ;
static uint32_t run_hz = RA8875_DEFAULT_SPI_HZ;
static gpio_line_t *reset_line = NULL;
static gpio_line_t *wait_line = NULL;
static uint8_t tx_bytes[BATCH_MAX][2];
static struct spi_ioc_transfer batch[BATCH_MAX];
static int batch_count = 0;
static bool reported_error = false;

static void sleep_us(long microseconds)
{
    struct timespec delay = { microseconds / 1000000L, (microseconds % 1000000L) * 1000L };
    nanosleep(&delay, NULL);
}

// WAIT is low while the chip is busy with a memory access: hold off sending.
static void wait_until_ready(void)
{
    if (wait_line == NULL) {
        return;
    }
    for (int waited_us = 0; waited_us < WAIT_PIN_TIMEOUT_MS * 1000; waited_us += 100) {
        if (gpio_get(wait_line) != 0) {
            return;
        }
        sleep_us(100);
    }
}

// Send everything queued. Each 2-byte frame is its own transfer (its own
// chip-select pulse), because the BeagleY-AI's SPI controller does not
// release CS between transfers batched into one call (tested on the board:
// batched frames were ignored; one frame per call works).
static void flush(void)
{
    if (batch_count == 0 || spi_fd < 0) {
        batch_count = 0;
        return;
    }
    spi_bus_lock();                         // the joystick ADC waits until this command is done
    wait_until_ready();
    for (int i = 0; i < batch_count; i++) {
        long delay = batch[i].delay_usecs;    // wait requested after this frame
        batch[i].delay_usecs = 0;
        batch[i].cs_change = 0;
        if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &batch[i]) < 0 && !reported_error) {
            fprintf(stderr, "ra8875: SPI transfer failed: %s\n", strerror(errno));
            reported_error = true;
        }
        if (delay > 0) {
            sleep_us(delay);
        }
    }
    spi_bus_unlock();
    batch_count = 0;
}

static void push(uint8_t prefix, uint8_t value)
{
    if (batch_count == BATCH_MAX) {
        flush();
    }
    tx_bytes[batch_count][0] = prefix;
    tx_bytes[batch_count][1] = value;
    struct spi_ioc_transfer *t = &batch[batch_count];
    memset(t, 0, sizeof(*t));
    t->tx_buf = (unsigned long)tx_bytes[batch_count];
    t->len = 2;
    t->speed_hz = current_hz;
    t->bits_per_word = 8;
    batch_count++;
}

// Wait after the last queued frame (done by flush(), right after sending it).
static void pause_us(long microseconds)
{
    if (batch_count > 0) {
        struct spi_ioc_transfer *last = &batch[batch_count - 1];
        long total = (long)last->delay_usecs + microseconds;
        if (total <= MAX_KERNEL_DELAY_US) {
            last->delay_usecs = (uint16_t)total;
            return;
        }
    }
    flush();
    sleep_us(microseconds);
}

static void command(uint8_t reg) { push(RA8875_CMD_WRITE, reg); }
static void data(uint8_t value)  { push(RA8875_DATA_WRITE, value); }
static void write_reg(uint8_t reg, uint8_t value) { command(reg); data(value); }

static void write_reg16(uint8_t reg_low, int value)   // low byte, then high byte in reg_low + 1
{
    write_reg(reg_low, (uint8_t)(value & 0xFF));
    write_reg((uint8_t)(reg_low + 1), (uint8_t)((value >> 8) & 0xFF));
}

// RGB565 -> the three colour registers (5-bit red, 6-bit green, 5-bit blue).
static void set_color(uint8_t first_reg, uint16_t color)
{
    write_reg(first_reg,     (uint8_t)((color >> 11) & 0x1F));
    write_reg(first_reg + 1, (uint8_t)((color >> 5) & 0x3F));
    write_reg(first_reg + 2, (uint8_t)(color & 0x1F));
}

static int clamp(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

// ---------------------------------------------------------------------------
bool ra8875_init(uint32_t spi_hz)
{
    run_hz = (spi_hz != 0) ? spi_hz : RA8875_DEFAULT_SPI_HZ;
    spi_fd = open(RA8875_SPI_DEVICE, O_RDWR);
    if (spi_fd < 0) {
        fprintf(stderr, "ra8875: cannot open %s: %s\n", RA8875_SPI_DEVICE, strerror(errno));
        fprintf(stderr, "        Is SPI enabled (spidev0.1 exists?), and are you running with sudo?\n");
        return false;
    }
    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    ioctl(spi_fd, SPI_IOC_WR_MODE, &mode);
    ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &run_hz);   // each transfer also sets its own speed

    // Hardware reset: RST low for 100 ms, then let the chip start (100 ms).
    reset_line = gpio_open_output(RA8875_RESET_LINE, "tour-display-rst", 1);
    wait_line = gpio_open_input(RA8875_WAIT_LINE, "tour-display-wait", true, false);
    if (reset_line != NULL) {
        gpio_set(reset_line, 0);
        sleep_us(100000);
        gpio_set(reset_line, 1);
        sleep_us(100000);
    } else {
        fprintf(stderr, "ra8875: no reset line - continuing without a hardware reset\n");
    }

    // ---- Set-up for an 800 x 480 panel (values as in Adafruit's RA8875 library) ----
    current_hz = INIT_SPI_HZ;
    write_reg(REG_PLLC1, 0x0B);              // PLL: input divider 1, multiplier 11
    pause_us(1000);
    write_reg(REG_PLLC2, 0x02);              // PLL output divided by 4
    pause_us(1000);
    write_reg(REG_SYSR, 0x0C);               // 16 bits per pixel, 8-bit interface
    write_reg(REG_PCSR, 0x81);               // pixel clock: falling edge, system clock / 2
    pause_us(1000);
    // Horizontal timing: width / 8 - 1, non-display, start, pulse width
    write_reg(REG_HDWR, RA8875_WIDTH / 8 - 1);
    write_reg(REG_HNDFTR, 0x00);
    write_reg(REG_HNDR, (26 - 2) / 8);
    write_reg(REG_HSTR, 32 / 8 - 1);
    write_reg(REG_HPWR, 96 / 8 - 1);
    // Vertical timing
    write_reg16(REG_VDHR0, RA8875_HEIGHT - 1);
    write_reg16(REG_VNDR0, 32 - 1);
    write_reg16(REG_VSTR0, 23 - 1);
    write_reg(REG_VPWR, 2 - 1);
    // Active window = whole screen
    write_reg16(REG_HSAW0, 0);
    write_reg16(REG_VSAW0, 0);
    write_reg16(REG_HEAW0, RA8875_WIDTH - 1);
    write_reg16(REG_VEAW0, RA8875_HEIGHT - 1);
    write_reg(REG_MCLR, 0x80);               // clear the whole screen memory
    pause_us(500000);
    flush();

    // ---- Normal speed from here on ----
    current_hz = run_hz;
    write_reg(REG_PWRR, 0x80);               // display on
    write_reg(REG_GPIOX, 0x01);              // enable the TFT (Adafruit board)
    write_reg(REG_P1CR, 0x80 | 0x0A);        // backlight PWM on, clock / 1024
    write_reg(REG_P1DCR, 255);               // full brightness
    flush();
    ra8875_fill_rect(0, 0, RA8875_WIDTH, RA8875_HEIGHT, 0x0000);
    return true;
}

void ra8875_cleanup(void)
{
    if (spi_fd >= 0) {
        write_reg(REG_P1DCR, 0);             // backlight off
        write_reg(REG_PWRR, 0x00);           // display off
        flush();
        close(spi_fd);
        spi_fd = -1;
    }
    gpio_close(reset_line);
    gpio_close(wait_line);
    reset_line = wait_line = NULL;
}

void ra8875_backlight(uint8_t level)
{
    write_reg(REG_P1DCR, level);
    flush();
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void ra8875_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    int x0 = clamp(x, 0, RA8875_WIDTH - 1);
    int y0 = clamp(y, 0, RA8875_HEIGHT - 1);
    int x1 = clamp(x + w - 1, 0, RA8875_WIDTH - 1);
    int y1 = clamp(y + h - 1, 0, RA8875_HEIGHT - 1);
    if (w <= 0 || h <= 0 || x1 < x0 || y1 < y0) {
        return;
    }
    write_reg16(REG_DLHSR0, x0);             // 0x91 / 0x92
    write_reg16(REG_DLHSR0 + 2, y0);         // 0x93 / 0x94
    write_reg16(REG_DLHSR0 + 4, x1);         // 0x95 / 0x96
    write_reg16(REG_DLHSR0 + 6, y1);         // 0x97 / 0x98
    set_color(REG_FGCR0, color);
    write_reg(REG_DCR, DCR_START_FILLED_RECT);
    // wait = base + pixels / speed
    long pixels = (long)(x1 - x0 + 1) * (long)(y1 - y0 + 1);
    pause_us(DRAW_BASE_DELAY_US + pixels / FILL_PIXELS_PER_US);
    flush();
}

void ra8875_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    x0 = clamp(x0, 0, RA8875_WIDTH - 1);  y0 = clamp(y0, 0, RA8875_HEIGHT - 1);
    x1 = clamp(x1, 0, RA8875_WIDTH - 1);  y1 = clamp(y1, 0, RA8875_HEIGHT - 1);
    x2 = clamp(x2, 0, RA8875_WIDTH - 1);  y2 = clamp(y2, 0, RA8875_HEIGHT - 1);
    write_reg16(REG_DLHSR0, x0);
    write_reg16(REG_DLHSR0 + 2, y0);
    write_reg16(REG_DLHSR0 + 4, x1);
    write_reg16(REG_DLHSR0 + 6, y1);
    write_reg16(REG_DTPH0, x2);              // 0xA9 / 0xAA
    write_reg16(REG_DTPH0 + 2, y2);          // 0xAB / 0xAC
    set_color(REG_FGCR0, color);
    write_reg(REG_DCR, DCR_START_FILLED_TRIANGLE);
    int min_x = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int max_x = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int min_y = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int max_y = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    long pixels = (long)(max_x - min_x + 1) * (long)(max_y - min_y + 1);   // bounding box (generous)
    pause_us(DRAW_BASE_DELAY_US + pixels / FILL_PIXELS_PER_US);
    flush();
}

void ra8875_text(int x, int y, int scale, uint16_t fg, uint16_t bg, const char *text)
{
    scale = clamp(scale, 0, 3);
    int char_w = 8 * (scale + 1);
    int char_h = 16 * (scale + 1);
    if (x < 0 || y < 0 || y + char_h > RA8875_HEIGHT || x >= RA8875_WIDTH) {
        return;
    }
    int fit = (RA8875_WIDTH - x) / char_w;    // characters that fit before the right edge

    write_reg(REG_MWCR0, 0x80);              // text mode
    write_reg(REG_FNCR0, 0x00);              // built-in font, ISO 8859-1
    write_reg(REG_FNCR1, (uint8_t)((scale << 2) | scale));   // enlarge x and y; opaque background
    write_reg16(REG_FCURXL, x);
    write_reg16(REG_FCURYL, y);
    set_color(REG_FGCR0, fg);
    set_color(REG_BGCR0, bg);
    command(REG_MRWC);
    long delay = (scale == 0) ? TEXT_CHAR_DELAY_SMALL_US
               : (scale == 1) ? TEXT_CHAR_DELAY_US : TEXT_CHAR_DELAY_BIG_US;
    for (int i = 0; text[i] != '\0' && i < fit; i++) {
        data((uint8_t)text[i]);
        pause_us(delay);
    }
    write_reg(REG_MWCR0, 0x00);              // back to graphics mode
    pause_us(DRAW_BASE_DELAY_US);
    flush();
}
