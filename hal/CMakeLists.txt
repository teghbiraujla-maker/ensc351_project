# HAL library: hardware access only (SPI ADC, joystick, audio device discovery).
# Use headers as: #include "hal/joystick.h"
file(GLOB HAL_SOURCES "src/*.c")
add_library(hal STATIC ${HAL_SOURCES})
target_include_directories(hal PUBLIC include)
