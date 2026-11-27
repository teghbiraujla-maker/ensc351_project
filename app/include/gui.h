// gui.h
// The tour's screen (800 x 480), drawn from tour_controller_view() each frame.
//
//   +-----------------------------+------------------------------+  y = 0
//   | NOW PLAYING - STOP 5 OF 9   |  1  Transportation Centre    |
//   | AQ                          |  ...  (scrolling list,       |
//   |                             |  highlighted = encoder pick, |
//   | controls reminder           |  > marks the playing stop)   |
//   +-----------------------------+------------------------------+  y = 320
//   | [=============== progress bar, full width ...........    ] |
//   |  2:41/5:07                 > / ||                 VOL  70% |
//   +------------------------------------------------------------+  y = 480
//
// Directions and "tour complete" appear as a popup over most of the screen,
// with "Press down on the joystick to continue" underneath.
//
// Only what changed since the last frame is redrawn. Nothing is ever erased
// before being redrawn, so updates never flicker.
//
// Background repair: gui_refresh_step() quietly repaints one small piece of
// the screen (with the same content). Called every GUI_REFRESH_STEP_MS, it
// repaints the whole screen every few seconds, which fixes any pixels a bad
// SPI transfer corrupted.

#ifndef _GUI_H_
#define _GUI_H_

#include "canvas.h"
#include "tour_controller.h"

#define GUI_WIDTH  800
#define GUI_HEIGHT 480
#define GUI_REFRESH_STEP_MS 200    // 18 pieces x 0.2 s: whole screen about every 3.6 s

void gui_init(const canvas_t *canvas);

// Draw the frame for `view` (only the parts that changed).
void gui_render(const tour_view_t *view);

// Force the next gui_render() to redraw everything.
void gui_invalidate(void);

// Repaint the next piece of the screen for `view` (call after gui_render()).
void gui_refresh_step(const tour_view_t *view);

// How many steps one full repair cycle of this screen takes.
int gui_refresh_piece_count(const tour_view_t *view);

// Word-wrap `text` into lines of at most `width_chars` characters (breaks at
// spaces; '\n' starts a new line; over-long words are split). Returns the
// number of lines written to `lines` (each line_size bytes), at most max_lines;
// *truncated is set if text did not fit. Exposed for the unit test.
int gui_wrap_text(const char *text, int width_chars, char *lines, int line_size,
                  int max_lines, int *truncated);

#endif
