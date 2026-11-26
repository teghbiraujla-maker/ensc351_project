# Hardware-free tests (build with -DBUILD_HOST_TESTS=ON on the host).
#   ctest --test-dir build-host --output-on-failure
include_directories(${CMAKE_SOURCE_DIR}/hal/include ${CMAKE_SOURCE_DIR}/app/include)

add_executable(audio_device_test audio_device_test.c ${CMAKE_SOURCE_DIR}/hal/src/audio_device.c)

# Encoder button: short / long press and debounce (pure logic)
add_executable(press_detector_test press_detector_test.c ${CMAKE_SOURCE_DIR}/hal/src/press_detector.c)

# Playback time / progress formatting (pure logic)
add_executable(playback_display_test playback_display_test.c ${CMAKE_SOURCE_DIR}/app/src/playback_display.c)

# Encoder control scheme against a fake audio player
add_executable(tour_controller_test tour_controller_test.c
  ${CMAKE_SOURCE_DIR}/app/src/tour_controller.c
  ${CMAKE_SOURCE_DIR}/app/src/playback_display.c
  ${CMAKE_SOURCE_DIR}/app/src/track_list.c
  ${CMAKE_SOURCE_DIR}/app/src/app_log.c)
target_link_libraries(tour_controller_test PRIVATE Threads::Threads)

# Typed debug commands -> encoder-style events
add_executable(terminal_input_test terminal_input_test.c
  ${CMAKE_SOURCE_DIR}/app/src/terminal_input.c
  ${CMAKE_SOURCE_DIR}/app/src/app_log.c)
target_link_libraries(terminal_input_test PRIVATE Threads::Threads)

# Thread-safe event queue
add_executable(input_queue_test input_queue_test.c ${CMAKE_SOURCE_DIR}/app/src/input_queue.c)
target_link_libraries(input_queue_test PRIVATE Threads::Threads)

enable_testing()
add_test(NAME audio_device COMMAND audio_device_test)
add_test(NAME press_detector COMMAND press_detector_test)
add_test(NAME playback_display COMMAND playback_display_test)
add_test(NAME tour_controller COMMAND tour_controller_test)
add_test(NAME terminal_input COMMAND terminal_input_test)
add_test(NAME input_queue COMMAND input_queue_test)
