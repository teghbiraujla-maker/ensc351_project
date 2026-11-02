#include "hal/joystick.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

static bool is_initialized = false;
static int spi_fd = -1;

void joystick_init(void) {
    printf("Joystick - Initializing");
    spi_fd = open("/dev/spidev0.0", O_RDWR);
    assert(!is_initialized);
    if (spi_fd < 0) {
        perror("ERROR: Failed to open SPI device for joystick");
        return;
    }
    is_initialized = true;
}

static int mcp3208_read_channel(int channel) {
    if (spi_fd < 0) return 0;

    unsigned char tx[3] = {
        0x06 | ((channel & 4) >> 2),
        (channel & 3) << 6,
        0x00
    };
    unsigned char rx[3] = {0};
        struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = 3,
        .speed_hz = 100000,
        .bits_per_word = 8,
    };
    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        perror("SPI transfer failed");
        return 0;
    }
    int adc_value = ((rx[1] & 0x0F) << 8) | rx[2];
    return adc_value;
}

int joystick_read_raw_x(void) {
    assert(is_initialized);
    return mcp3208_read_channel(0);
}

int joystick_read_raw_y(void) {
    assert(is_initialized);
    return mcp3208_read_channel(1);
}

void joystick_cleanup(void) {
    assert(is_initialized);
    if (spi_fd >= 0) {
        close(spi_fd);
    }
    is_initialized = false;
}
