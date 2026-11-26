// app_log.h
// All messages for the user go through here instead of printf().
//
// Each message is
//   - kept in memory (the last APP_LOG_CAPACITY lines), so the GUI can show
//     them in its "terminal" panel without the program printing over the screen;
//   - echoed to stdout (the SSH terminal) unless that is turned off;
//   - appended to a log file, if one was given (handy for debugging the GUI).
//
// Thread-safe. Messages containing '\n' are split into several lines.

#ifndef _APP_LOG_H_
#define _APP_LOG_H_

#include <stdbool.h>
#include <stddef.h>

#define APP_LOG_CAPACITY   200   // lines kept in memory
#define APP_LOG_LINE_CHARS 128   // longer lines are cut

// log_file_path may be NULL (no file). Returns false if the file cannot be opened
// (logging to memory and stdout still works).
bool app_log_init(const char *log_file_path, bool echo_to_stdout);
void app_log_cleanup(void);

// printf-style message.
void app_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

// Record what the user typed, shown as "> text". Not echoed to stdout
// (the terminal already shows what was typed).
void app_log_input(const char *typed_text);

// For the GUI: number of lines held, and a copy of one line
// (index 0 = oldest held). Returns false if index is out of range.
int app_log_line_count(void);
bool app_log_get_line(int index, char *out, size_t out_size);

// Increases every time a line is added, so the GUI can tell when to redraw.
unsigned long app_log_version(void);

#endif
