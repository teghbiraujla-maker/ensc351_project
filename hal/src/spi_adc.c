#include "hal/spi_adc.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/spi/spidev.h>

// Can be overridden at build time (-DSPI_ADC_DEVICE=\"/dev/spidev0.1\" etc.)
#ifndef SPI_ADC_DEVICE
#define SPI_ADC_DEVICE "/dev/spidev0.0"
#endif
// 100 kHz matches the maximum speed of the SPI overlay used on the course image
// and is well inside the MCP3208's limits.
#ifndef SPI_ADC_SPEED_HZ
#define SPI_ADC_SPEED_HZ 100000
#endif
#define SPI_ADC_MODE 0
#define SPI_ADC_BITS 8

static bool is_initialized = false;
static int spi_fd = -1;

static bool configure(void)
{
    uint8_t mode = SPI_ADC_MODE;
    uint8_t bits = SPI_ADC_BITS;
    uint32_t speed = SPI_ADC_SPEED_HZ;

    if (ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) == -1
        || ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) == -1
        || ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) == -1) {
        fprintf(stderr, "spi_adc: cannot configure %s: %s\n", SPI_ADC_DEVICE, strerror(errno));
        return false;
    }
    return true;
}

bool spi_adc_init(void)
{
    assert(!is_initialized);

    spi_fd = open(SPI_ADC_DEVICE, O_RDWR);
    if (spi_fd < 0) {
        fprintf(stderr, "spi_adc: cannot open %s: %s\n", SPI_ADC_DEVICE, strerror(errno));
        fprintf(stderr, "         Is SPI enabled on this board, and are you running with sudo?\n");
        return false;
    }
    if (!configure()) {
        close(spi_fd);
        spi_fd = -1;
        return false;
    }
    is_initialized = true;
    return true;
}

int spi_adc_read(int channel)
{
    assert(is_initialized);
    assert(channel >= 0 && channel < SPI_ADC_CHANNEL_COUNT);

    // MCP3208 request (3 bytes): start bit, single-ended, channel bits D2..D0.
    //   byte 0: 0000 0110 | D2     byte 1: D1 D0 00 0000     byte 2: don't care
    uint8_t tx[3] = {
        (uint8_t)(0x06 | ((channel & 0x04) >> 2)),
        (uint8_t)((channel & 0x03) << 6),
        0x00
    };
    uint8_t rx[3] = { 0 };

    struct spi_ioc_transfer transfer;
    memset(&transfer, 0, sizeof(transfer));
    transfer.tx_buf = (uintptr_t)tx;
    transfer.rx_buf = (uintptr_t)rx;
    transfer.len = sizeof(tx);
    transfer.speed_hz = SPI_ADC_SPEED_HZ;
    transfer.bits_per_word = SPI_ADC_BITS;

    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &transfer) < 1) {
        fprintf(stderr, "spi_adc: transfer failed: %s\n", strerror(errno));
        return -1;
    }
    // Low 4 bits of rx[1] are result bits 11..8; rx[2] holds bits 7..0.
    return ((rx[1] & 0x0F) << 8) | rx[2];
}

void spi_adc_cleanup(void)
{
    assert(is_initialized);
    close(spi_fd);
    spi_fd = -1;
    is_initialized = false;
}
