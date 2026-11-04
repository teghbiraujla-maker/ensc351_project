// spi_adc.h
// HAL module: read single-ended channels of the MCP3208 12-bit ADC over SPI.
//
// The kernel exposes the ADC's SPI bus as /dev/spidev0.0, which normally
// needs root (run programs with sudo).

#ifndef _SPI_ADC_H_
#define _SPI_ADC_H_

#include <stdbool.h>

#define SPI_ADC_MAX_VALUE     4095   // 12-bit converter: 0 .. 4095
#define SPI_ADC_CHANNEL_COUNT 8      // MCP3208 has 8 single-ended channels

// Open and configure the SPI device. Returns false (after printing the
// reason to stderr) if the device cannot be opened or configured.
bool spi_adc_init(void);

// Read one channel. Returns 0 .. SPI_ADC_MAX_VALUE, or -1 if the transfer fails.
int spi_adc_read(int channel);

void spi_adc_cleanup(void);

#endif
