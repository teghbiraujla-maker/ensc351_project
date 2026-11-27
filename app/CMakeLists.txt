# Application programs. All hardware access goes through the HAL library.
include_directories(include)

# Main program: the audio tour (encoder + joystick + screen)
add_executable(tour_player
  src/main.c
  src/app_log.c
  src/audio_player.c
  src/gui.c
  src/guide_text.c
  src/input_queue.c
  src/joystick_input.c
  src/playback_display.c
  src/saved_state.c
  src/terminal_input.c
  src/tour_controller.c
  src/track_list.c)
target_link_libraries(tour_player PRIVATE hal Threads::Threads)

# Hardware checks
add_executable(audio_selftest test/audio_selftest.c src/audio_player.c src/track_list.c)
target_link_libraries(audio_selftest PRIVATE hal Threads::Threads)

add_executable(joystick_test test/joystick_test.c)
target_link_libraries(joystick_test PRIVATE hal)

add_executable(display_test test/display_test.c)
target_link_libraries(display_test PRIVATE hal)

# Put a copy of the audio next to the programs so they can be run from build/app
add_custom_command(TARGET tour_player POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/audio ${CMAKE_CURRENT_BINARY_DIR}/audio
  COMMENT "Copying audio files next to the executables")
