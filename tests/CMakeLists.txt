# Hardware-free tests (build with -DBUILD_HOST_TESTS=ON on the host).
include_directories(${CMAKE_SOURCE_DIR}/hal/include)

# Joystick logic against a fake ADC (so this test supplies spi_adc_*, and the real spi_adc.c is not linked)
add_executable(joystick_logic_test joystick_logic_test.c ${CMAKE_SOURCE_DIR}/hal/src/joystick.c)

add_executable(audio_device_test audio_device_test.c ${CMAKE_SOURCE_DIR}/hal/src/audio_device.c)

# Playback time formatting (pure logic)
add_executable(playback_display_test playback_display_test.c ${CMAKE_SOURCE_DIR}/app/src/playback_display.c)
target_include_directories(playback_display_test PRIVATE ${CMAKE_SOURCE_DIR}/app/include)

enable_testing()
add_test(NAME joystick_logic COMMAND joystick_logic_test)
add_test(NAME audio_device COMMAND audio_device_test)
add_test(NAME playback_display COMMAND playback_display_test)
