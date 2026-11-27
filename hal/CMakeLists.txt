# HAL library: hardware access only (GPIO, rotary encoder, joystick + SPI ADC,
# RA8875 screen, audio device discovery). Use headers as: #include "hal/encoder.h"
set(HAL_SOURCES
  src/audio_device.c
  src/encoder.c
  src/joystick.c
  src/press_detector.c
  src/ra8875.c
  src/spi_adc.c
  src/spi_bus.c)

if(ENABLE_GPIO)
  # libgpiod v2. For cross-compiling, install the arm64 version on the host:
  #   sudo dpkg --add-architecture arm64 && sudo apt update
  #   sudo apt install libgpiod-dev:arm64
  find_path(GPIOD_INCLUDE_DIR gpiod.h)
  find_library(GPIOD_LIBRARY gpiod)
  if(NOT GPIOD_INCLUDE_DIR OR NOT GPIOD_LIBRARY)
    message(FATAL_ERROR
      "libgpiod (v2) not found for this compiler.\n"
      "Cross-compiling: sudo dpkg --add-architecture arm64 && sudo apt update && "
      "sudo apt install libgpiod-dev:arm64\n"
      "Or build without GPIO: -DENABLE_GPIO=OFF")
  endif()
  list(APPEND HAL_SOURCES src/gpio.c)
else()
  list(APPEND HAL_SOURCES src/gpio_stub.c)
endif()

add_library(hal STATIC ${HAL_SOURCES})
target_include_directories(hal PUBLIC include)
target_link_libraries(hal PUBLIC Threads::Threads)
if(ENABLE_GPIO)
  target_include_directories(hal PRIVATE ${GPIOD_INCLUDE_DIR})
  target_link_libraries(hal PUBLIC ${GPIOD_LIBRARY})
endif()
