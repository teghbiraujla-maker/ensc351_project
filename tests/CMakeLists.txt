# Hardware-free tests (build with -DBUILD_HOST_TESTS=ON on the host).
#   ctest --test-dir build-host --output-on-failure
include_directories(${CMAKE_SOURCE_DIR}/hal/include ${CMAKE_SOURCE_DIR}/app/include)
set(APP ${CMAKE_SOURCE_DIR}/app/src)
set(HAL ${CMAKE_SOURCE_DIR}/hal/src)

add_executable(audio_device_test audio_device_test.c ${HAL}/audio_device.c)

# Encoder / joystick button: short / long press and debounce
add_executable(press_detector_test press_detector_test.c ${HAL}/press_detector.c)

# Joystick direction logic against a fake ADC (this test supplies spi_adc_*)
add_executable(joystick_logic_test joystick_logic_test.c ${HAL}/joystick.c ${HAL}/gpio_stub.c)

# Joystick -> control events (repeat rates, skip release, button)
add_executable(joystick_input_test joystick_input_test.c ${APP}/joystick_input.c
  ${HAL}/press_detector.c ${HAL}/joystick.c ${HAL}/gpio_stub.c ${HAL}/spi_adc.c ${HAL}/spi_bus.c)
target_link_libraries(joystick_input_test PRIVATE Threads::Threads)

# Time / progress formatting
add_executable(playback_display_test playback_display_test.c ${APP}/playback_display.c)

# The control rules against a fake audio player
add_executable(tour_controller_test tour_controller_test.c ${APP}/tour_controller.c
  ${APP}/playback_display.c ${APP}/guide_text.c ${APP}/track_list.c ${APP}/app_log.c)
target_link_libraries(tour_controller_test PRIVATE Threads::Threads)

# Typed debug commands -> control events
add_executable(terminal_input_test terminal_input_test.c ${APP}/terminal_input.c ${APP}/app_log.c)
target_link_libraries(terminal_input_test PRIVATE Threads::Threads)

# Thread-safe event queue
add_executable(input_queue_test input_queue_test.c ${APP}/input_queue.c)
target_link_libraries(input_queue_test PRIVATE Threads::Threads)

# Save / resume file
add_executable(saved_state_test saved_state_test.c ${APP}/saved_state.c)

# Directions text files
add_executable(guide_text_test guide_text_test.c ${APP}/guide_text.c ${APP}/track_list.c)

# Screen layout (draws into a recorder; --dump DIR writes previews' data)
add_executable(gui_test gui_test.c ${APP}/gui.c ${APP}/guide_text.c ${APP}/track_list.c
  ${APP}/app_log.c ${APP}/playback_display.c)
target_link_libraries(gui_test PRIVATE Threads::Threads)

foreach(test audio_device press_detector joystick_logic joystick_input playback_display
             tour_controller terminal_input input_queue saved_state guide_text gui)
  add_test(NAME ${test} COMMAND ${test}_test)
endforeach()
