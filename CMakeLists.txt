# Campus tour - build configuration.
#
# Cross-compile for the BeagleY-AI (from the host):
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake
#   cmake --build build
# (scripts/build.sh does exactly this.)
#
# Run the hardware-free tests on the host:
#   cmake -S . -B build-host -DBUILD_HOST_TESTS=ON && cmake --build build-host
cmake_minimum_required(VERSION 3.18)
project(campus_tour VERSION 0.1 DESCRIPTION "SFU Burnaby interactive campus tour" LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_compile_definitions(_POSIX_C_SOURCE=200809L)
add_compile_options(-Wall -Wextra -Wpedantic)

option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
if(WARNINGS_AS_ERRORS)
  add_compile_options(-Werror)
endif()

option(ENABLE_ASAN "Build with the address sanitizer (target needs libasan installed)" OFF)
if(ENABLE_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()

option(BUILD_HOST_TESTS "Build the hardware-free unit tests (use on the host, not for the board)" OFF)

# Joystick wiring/polarity - change these if your joystick is wired differently
# (or pass -DJOYSTICK_Y_UP_IS_HIGH=0 on the cmake command line).
set(JOYSTICK_X_CHANNEL 0 CACHE STRING "ADC channel wired to the joystick's Xout")
set(JOYSTICK_Y_CHANNEL 1 CACHE STRING "ADC channel wired to the joystick's Yout")
set(JOYSTICK_Y_UP_IS_HIGH 1 CACHE STRING "1 if pushing the stick UP raises the ADC value, else 0")
set(JOYSTICK_X_RIGHT_IS_HIGH 1 CACHE STRING "1 if pushing the stick RIGHT raises the ADC value, else 0")
add_compile_definitions(
  JOYSTICK_X_CHANNEL=${JOYSTICK_X_CHANNEL}
  JOYSTICK_Y_CHANNEL=${JOYSTICK_Y_CHANNEL}
  JOYSTICK_Y_UP_IS_HIGH=${JOYSTICK_Y_UP_IS_HIGH}
  JOYSTICK_X_RIGHT_IS_HIGH=${JOYSTICK_X_RIGHT_IS_HIGH})

find_package(Threads REQUIRED)

if(BUILD_HOST_TESTS)
  add_subdirectory(tests)
else()
  add_subdirectory(hal)
  add_subdirectory(app)
endif()
