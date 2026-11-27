// spi_bus.h
// The joystick ADC (CE0) and the screen (CE1) share one SPI bus. This lock
// keeps them from taking turns in the middle of each other's work: a whole
// screen drawing command (several transfers) finishes before the joystick
// thread reads the ADC, and vice versa.

#ifndef _SPI_BUS_H_
#define _SPI_BUS_H_

void spi_bus_lock(void);
void spi_bus_unlock(void);

#endif
