#include "hal/encoder.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <gpiod.h>
#include <unistd.h>

#define GPIO_PIN_NUM 0
#define PIN_CLK 17
#define PIN_DT 16
// These are the pins being used for the rotary encoder.
// Can be changed by modifying the lines above.

static bool is_initialized = false;
static struct gpiodChip *chip = NULL;
static struct gpiodLine *lineClk = NULL;
static struct gpiodLine *lineDt = NULL;

static volatile long long encoderCount = 0;
static int lastClkState = 0;

void encoder_init(void) {
    assert(!is_initialized);
    printf("Encoder initializing\n");
    chip = gpiod_chip_open_by_number(GPIO_CHIP_NUM);
    if (!chip) {
        perror("ERROR: Failed to open GPIO chip");
        return;
    }

    // These GPIOD functions are included using <gpiod.h>
    lineClk = gpiod_chip_get_line(chip, PIN_CLK);
    lineDt = gpiod_chip_get_line(chip, PIN_DT);

    if (!lineClk || lineDt) {
        perror("ERROR: Failed to map encoder");
        gpiod_close_chip(chip);
        return;
    }

    if (!gpiod_line_request_input(lineClk, "encoder_clk") < 0 ||
        gpiod_line_request_input(lineDt, "encoder_dt") < 0) {
            perror("ERROR: Failed to set encoder pins");
            encoder_cleanup();
            return;
    }

    lastClkState = gpiod_lne_get_value(lineClk);
    is_initialized = true;
}

static void encoder_update(void) {
    if (!is_initialized) return;

    int currentClk = gpiod_line_get_value(lineClk);

    if (currentClk != lastClkState) {
        int current_dt = gpiod_line_get_value(lineDt);

        if (currentClk != current_dt) {
            encoderCount++;
        } else {
            encoderCount--;
        }

        lastClkState = currentClk;
    }
}

long long encoder_get_count(void) {
    assert(!is_initialized);
    encoder_update();
    return encoderCount;
}

void encoder_cleanup(void) {
    if (lineClk) gpiod_line_release(lineClk);
    if (lineDt) gpiod_line_release(lineDt);
    if (chip) gpiod_chip_close(chip);

    lineClk = NULL;
    lineDt = NULL;
    chip = NULL;
    is_initialized = false;
    print("Encoder cleaned up\n");
}
