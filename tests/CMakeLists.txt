# Hardware-free tests (build with -DBUILD_HOST_TESTS=ON on the host).
include_directories(${CMAKE_SOURCE_DIR}/hal/include)

# Joystick logic against a fake ADC (so this test supplies spi_adc_*, and the real spi_adc.c is not linked)
add_executable(joystick_logic_test joystick_logic_test.c ${CMAKE_SOURCE_DIR}/hal/src/joystick.c)

add_executable(audio_device_test audio_device_test.c ${CMAKE_SOURCE_DIR}/hal/src/audio_device.c)

# LCD screens and input mapping against a fake LCD and track list (this test supplies lcd_* and track_list_*)
add_executable(lcd_ui_test lcd_ui_test.c ${CMAKE_SOURCE_DIR}/app/src/lcd_ui.c)
target_include_directories(lcd_ui_test PRIVATE ${CMAKE_SOURCE_DIR}/app/include)
target_link_libraries(lcd_ui_test PRIVATE Threads::Threads)

enable_testing()
add_test(NAME joystick_logic COMMAND joystick_logic_test)
add_test(NAME audio_device COMMAND audio_device_test)
add_test(NAME lcd_ui COMMAND lcd_ui_test)
