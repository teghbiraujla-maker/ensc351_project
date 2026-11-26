// lcd_ui.h
// Decides what the LCD shows, from the input events and the player status.
//
// Every input the user can give is reflected on the screen:
//
//   input                      what the LCD does
//   -------------------------  ---------------------------------------------
//   encoder turn / "<" ">"     moves the highlight (">") in the stop list;
//                              the list is shown for a few seconds even while
//                              a recording is playing, then it returns to
//                              the now-playing screen
//   selection (track number    highlights that stop and shows "Playing: ...",
//   or "ok")                   or "No track N" / "Cannot play N" on failure
//   pause / resume             "Paused" / "Resumed"
//   stop                       "Stopped", back to the stop list
//   volume +/-                 "Volume N%" (the volume bar is always on the
//                              now-playing screen)
//   skip forward / back        "Skipped forward" / "Skipped back"
//   list / info / help         the list, the now-playing screen, or a short
//                              help screen for a few seconds
//
// Screens (20 x 4 shown; the layout adapts to LCD_COLS):
//
//   stop list                  now playing
//   +--------------------+     +--------------------+
//   |Select stop 2/5     |     |PLAYING 2/5         |
//   | * 1 Welcome to SFU |     |Library             |
//   |> 2 Library         |     |02:33 / 04:07       |
//   |  3 Convocation Mall|     |Vol 70% [#######---]|
//   +--------------------+     +--------------------+
//    '>' = highlighted, '*' = the stop that is playing
//
// A short message ("toast") replaces the top line of the stop list, or the
// volume line of the now-playing screen, for a couple of seconds.
//
// Other modules only use this interface: lcd_ui_show_message() is the hook for
// a boot/welcome message, and lcd_ui_refresh() draws the playback clock from
// the audio_status_t it is given (it never talks to the audio player itself).
//
// All functions are thread-safe (the encoder reports from its own thread).
// Until lcd_ui_init() is called, and after lcd_ui_cleanup(), they do nothing.

#ifndef _LCD_UI_H_
#define _LCD_UI_H_

#include <stdbool.h>
#include "audio_player.h"      // audio_status_t
#include "terminal_input.h"    // input_event_t

// Start the LCD and the UI state. Needs the track list to be loaded already
// (call track_list_load() first). Returns false if the LCD could not be
// started; the UI state still works then, so selection keeps working without
// a screen.
bool lcd_ui_init(void);

// Blank the screen and release the LCD.
void lcd_ui_cleanup(void);

// Move the highlight in the stop list: +1 = next stop, -1 = previous. Wraps
// around at the ends. Called from the encoder callback.
void lcd_ui_move_cursor(int delta);

// 0-based index of the highlighted stop (what "select" should play).
int lcd_ui_cursor(void);

// Show the effect of an input event. Call it after the event has been handled,
// with the track that is now current (-1 = none) and the player status. Also
// handles the cursor events (INPUT_CURSOR_UP / INPUT_CURSOR_DOWN).
void lcd_ui_on_input(input_event_t event, int current_track, const audio_status_t *status);

// Show a short message for a couple of seconds on the screen that is showing.
void lcd_ui_notify(const char *text);

// Take over the whole screen with up to two centred lines for duration_ms
// (e.g. the welcome message at boot). line2 may be NULL.
void lcd_ui_show_message(const char *line1, const char *line2, int duration_ms);

// Redraw from the latest status. Call it regularly (the main loop does it
// every poll) so the clock and the volume stay current and messages expire.
void lcd_ui_refresh(int current_track, const audio_status_t *status);

// For tests: replace the millisecond clock (NULL restores the real one).
void lcd_ui_set_clock(long (*now_ms)(void));

#endif
