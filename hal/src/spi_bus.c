#include "hal/spi_bus.h"
#include <pthread.h>

static pthread_mutex_t bus_mutex = PTHREAD_MUTEX_INITIALIZER;

void spi_bus_lock(void)   { pthread_mutex_lock(&bus_mutex); }
void spi_bus_unlock(void) { pthread_mutex_unlock(&bus_mutex); }
