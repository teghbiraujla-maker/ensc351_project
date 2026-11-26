# Application programs. All hardware access goes through the HAL library.
include_directories(include)

# Main program: the audio tour player
add_executable(tour_player
  src/main.c
  src/audio_player.c
  src/track_list.c
  src/playback_display.c
  src/progress_store.c
  src/terminal_input.c)
target_link_libraries(tour_player PRIVATE hal Threads::Threads)

# Hardware check: live joystick display
add_executable(joystick_test test/joystick_test.c)
target_link_libraries(joystick_test PRIVATE hal)

# Hardware check: end-to-end audio test
add_executable(audio_selftest
  test/audio_selftest.c
  src/audio_player.c
  src/track_list.c)
target_link_libraries(audio_selftest PRIVATE hal Threads::Threads)

# Put a copy of the sample audio next to the programs so they can be run from build/app
add_custom_command(TARGET tour_player POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/audio ${CMAKE_CURRENT_BINARY_DIR}/audio
  COMMENT "Copying audio files next to the executables")
