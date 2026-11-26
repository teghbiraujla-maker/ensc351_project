#include "lcd_ui.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "hal/lcd.h"
#include "track_list.h"

#define BROWSE_VIEW_MS      5000   // stop list stays up after the encoder moves
#define INFO_VIEW_MS        5000   // "list" / "info" commands
#define HELP_VIEW_MS        6000
#define TOAST_MS            2500
#define MESSAGE_DEFAULT_MS  3000
#define TEXT_CAPACITY       128
#define VOLUME_LABEL_WIDTH  10     // "Vol 100%[" plus the closing "]"

_Static_assert(LCD_ROWS >= 4, "lcd_ui needs an LCD with at least 4 rows");
_Static_assert(LCD_COLS >= 16, "lcd_ui needs an LCD with at least 16 columns");

typedef enum {
    VIEW_AUTO,       // not forced: list when stopped, now-playing otherwise
    VIEW_MENU,
    VIEW_PLAYING,
    VIEW_HELP,
    VIEW_MESSAGE,
} view_t;

static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool s_active = false;          // lcd_ui_init() has been called
static bool s_lcdOk = false;           // the LCD itself started
static long (*s_clock)(void) = NULL;

static int s_cursor = 0;
static int s_track = -1;               // current track (-1 = none), from the caller
static audio_status_t s_status;

static view_t s_forcedView = VIEW_AUTO;
static long s_forcedUntil = 0;
static char s_message[2][TEXT_CAPACITY];

static char s_toast[TEXT_CAPACITY];
static long s_toastUntil = 0;

static lcd_frame_t s_lastFrame;
static bool s_haveLastFrame = false;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static long real_now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long)now.tv_sec * 1000L + now.tv_nsec / 1000000L;
}

static long now_ms(void)
{
    return (s_clock != NULL) ? s_clock() : real_now_ms();
}

static void blank_frame(lcd_frame_t *frame)
{
    for (int row = 0; row < LCD_ROWS; row++) {
        memset(frame->row[row], ' ', LCD_COLS);
        frame->row[row][LCD_COLS] = '\0';
    }
}

// Write printf-style text to a row, cut to the width and padded with spaces.
static void set_row(lcd_frame_t *frame, int row, const char *format, ...)
{
    char text[TEXT_CAPACITY];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    bool ended = false;
    for (int col = 0; col < LCD_COLS; col++) {
        char c = ended ? ' ' : text[col];
        if (c == '\0') {
            ended = true;
            c = ' ';
        } else if ((unsigned char)c < 32) {
            c = '?';
        }
        frame->row[row][col] = c;
    }
    frame->row[row][LCD_COLS] = '\0';
}

static void set_row_centered(lcd_frame_t *frame, int row, const char *text)
{
    int length = (int)strlen(text);
    int pad = (length >= LCD_COLS) ? 0 : (LCD_COLS - length) / 2;
    set_row(frame, row, "%*s%s", pad, "", text);
}

static void format_clock(double seconds, char *out, size_t out_size)
{
    int total = (int)(seconds + 0.5);
    snprintf(out, out_size, "%02d:%02d", total / 60, total % 60);
}

static bool toast_active(long now)
{
    return s_toast[0] != '\0' && now < s_toastUntil;
}

static void set_toast_locked(const char *text)
{
    snprintf(s_toast, sizeof(s_toast), "%s", text);
    s_toastUntil = now_ms() + TOAST_MS;
}

static void force_view_locked(view_t view, long duration_ms)
{
    s_forcedView = view;
    s_forcedUntil = now_ms() + duration_ms;
}

static bool track_is_valid(int index)
{
    return index >= 0 && index < track_list_count();
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------
static void render_menu(lcd_frame_t *frame, long now)
{
    int total = track_list_count();
    if (total == 0) {
        set_row(frame, 0, "NO TRACKS FOUND");
        set_row(frame, 1, "Add MP3s to audio/");
        return;
    }

    if (toast_active(now)) {
        set_row(frame, 0, "%s", s_toast);
    } else {
        set_row(frame, 0, "Select stop %d/%d", s_cursor + 1, total);
    }

    int listRows = LCD_ROWS - 1;
    int top = s_cursor - listRows / 2;      // keep the highlight in the middle
    if (top > total - listRows) {
        top = total - listRows;
    }
    if (top < 0) {
        top = 0;
    }
    for (int r = 0; r < listRows; r++) {
        int index = top + r;
        if (index >= total) {
            break;
        }
        bool playing = (index == s_track && s_status.state != AUDIO_STOPPED);
        set_row(frame, 1 + r, "%c%c%2d %s",
                index == s_cursor ? '>' : ' ',
                playing ? '*' : ' ',
                index + 1, track_list_name(index));
    }
}

static void render_playing(lcd_frame_t *frame, long now)
{
    const char *name = track_is_valid(s_track) ? track_list_name(s_track) : "";
    set_row(frame, 0, "%s %d/%d",
            s_status.state == AUDIO_PAUSED ? "PAUSED" : "PLAYING",
            s_track + 1, track_list_count());
    set_row(frame, 1, "%s", name);

    char position[16];
    format_clock(s_status.position_sec, position, sizeof(position));
    if (s_status.duration_sec > 0.0) {
        char duration[16];
        format_clock(s_status.duration_sec, duration, sizeof(duration));
        set_row(frame, 2, "%s / %s", position, duration);
    } else {
        set_row(frame, 2, "%s / --:--", position);
    }

    if (toast_active(now)) {
        set_row(frame, 3, "%s", s_toast);
        return;
    }
    int barWidth = LCD_COLS - VOLUME_LABEL_WIDTH;
    int volume = s_status.volume_percent;
    if (volume < 0) {
        volume = 0;
    }
    if (volume > 100) {
        volume = 100;
    }
    int filled = (volume * barWidth + 50) / 100;
    char bar[LCD_COLS + 1];
    for (int i = 0; i < barWidth; i++) {
        bar[i] = (i < filled) ? '#' : '-';
    }
    bar[barWidth] = '\0';
    char volumeText[8];
    snprintf(volumeText, sizeof(volumeText), "%d%%", volume);
    set_row(frame, 3, "Vol %-4s[%s]", volumeText, bar);
}

static void render_help(lcd_frame_t *frame)
{
    set_row(frame, 0, "Turn: pick a stop");
    set_row(frame, 1, "ok: play that stop");
    set_row(frame, 2, "p pause   s stop");
    set_row(frame, 3, "+/- vol   f/b skip");
}

static void render_message(lcd_frame_t *frame)
{
    set_row_centered(frame, 1, s_message[0]);
    set_row_centered(frame, 2, s_message[1]);
}

static view_t choose_view_locked(long now)
{
    if (s_forcedView != VIEW_AUTO && now >= s_forcedUntil) {
        s_forcedView = VIEW_AUTO;
    }
    if (s_forcedView == VIEW_PLAYING && s_status.state == AUDIO_STOPPED) {
        s_forcedView = VIEW_AUTO;           // nothing left to show there
    }
    if (s_forcedView != VIEW_AUTO) {
        return s_forcedView;
    }
    return (s_status.state == AUDIO_STOPPED) ? VIEW_MENU : VIEW_PLAYING;
}

// Build the frame for the current state and send it if it changed.
static void render_locked(void)
{
    long now = now_ms();
    lcd_frame_t frame;
    blank_frame(&frame);

    switch (choose_view_locked(now)) {
    case VIEW_PLAYING:
        render_playing(&frame, now);
        break;
    case VIEW_HELP:
        render_help(&frame);
        break;
    case VIEW_MESSAGE:
        render_message(&frame);
        break;
    case VIEW_MENU:
    case VIEW_AUTO:
        render_menu(&frame, now);
        break;
    }

    if (s_haveLastFrame && memcmp(&frame, &s_lastFrame, sizeof(frame)) == 0) {
        return;                              // nothing changed: don't redraw
    }
    s_lastFrame = frame;
    s_haveLastFrame = true;
    if (s_lcdOk) {
        lcd_show_frame(&frame);
    }
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------
bool lcd_ui_init(void)
{
    pthread_mutex_lock(&s_mutex);
    s_cursor = 0;
    s_track = -1;
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = AUDIO_STOPPED;
    s_forcedView = VIEW_AUTO;
    s_toast[0] = '\0';
    s_haveLastFrame = false;
    s_lcdOk = lcd_init();
    s_active = true;
    render_locked();
    pthread_mutex_unlock(&s_mutex);
    return s_lcdOk;
}

void lcd_ui_cleanup(void)
{
    pthread_mutex_lock(&s_mutex);
    if (s_active) {
        if (s_lcdOk) {
            lcd_frame_t blank;
            blank_frame(&blank);
            lcd_show_frame(&blank);
            lcd_cleanup();
        }
        s_active = false;
        s_lcdOk = false;
    }
    pthread_mutex_unlock(&s_mutex);
}

static void move_cursor_locked(int delta)
{
    int total = track_list_count();
    if (total > 0) {
        s_cursor = ((s_cursor + delta) % total + total) % total;   // wraps both ways
    }
    s_toast[0] = '\0';                       // an old message must not hide the header
    force_view_locked(VIEW_MENU, BROWSE_VIEW_MS);
}

void lcd_ui_move_cursor(int delta)
{
    pthread_mutex_lock(&s_mutex);
    if (s_active) {
        move_cursor_locked(delta);
        render_locked();
    }
    pthread_mutex_unlock(&s_mutex);
}

int lcd_ui_cursor(void)
{
    pthread_mutex_lock(&s_mutex);
    int cursor = s_cursor;
    pthread_mutex_unlock(&s_mutex);
    return cursor;
}

void lcd_ui_on_input(input_event_t event, int current_track, const audio_status_t *status)
{
    pthread_mutex_lock(&s_mutex);
    if (!s_active || event.type == INPUT_NONE || event.type == INPUT_QUIT) {
        pthread_mutex_unlock(&s_mutex);
        return;
    }
    s_track = current_track;
    if (status != NULL) {
        s_status = *status;
    }
    bool stopped = (s_status.state == AUDIO_STOPPED);
    char text[TEXT_CAPACITY];

    switch (event.type) {
    case INPUT_CURSOR_UP:
        move_cursor_locked(-1);
        break;
    case INPUT_CURSOR_DOWN:
        move_cursor_locked(1);
        break;
    case INPUT_SELECT_TRACK: {
        int index = event.value - 1;
        if (!track_is_valid(index)) {
            snprintf(text, sizeof(text), "No track %d", event.value);
        } else if (index == current_track && !stopped) {
            s_cursor = index;
            s_forcedView = VIEW_AUTO;        // show the now-playing screen
            snprintf(text, sizeof(text), "Playing: %s", track_list_name(index));
        } else {
            snprintf(text, sizeof(text), "Cannot play %d", event.value);
        }
        set_toast_locked(text);
        break;
    }
    case INPUT_TOGGLE_PAUSE:
        if (stopped) {
            set_toast_locked("Nothing playing");
        } else {
            set_toast_locked(s_status.state == AUDIO_PAUSED ? "Paused" : "Resumed");
        }
        break;
    case INPUT_STOP:
        s_forcedView = VIEW_AUTO;
        set_toast_locked("Stopped");
        break;
    case INPUT_VOLUME_UP:
    case INPUT_VOLUME_DOWN:
        snprintf(text, sizeof(text), "Volume %d%%", s_status.volume_percent);
        set_toast_locked(text);
        break;
    case INPUT_SEEK_FORWARD:
    case INPUT_SEEK_BACK:
        if (stopped) {
            set_toast_locked("Nothing playing");
        } else {
            set_toast_locked(event.type == INPUT_SEEK_FORWARD ? "Skipped forward" : "Skipped back");
        }
        break;
    case INPUT_LIST:
        force_view_locked(VIEW_MENU, INFO_VIEW_MS);
        break;
    case INPUT_STATUS:
        if (stopped) {
            set_toast_locked("Nothing playing");
        } else {
            force_view_locked(VIEW_PLAYING, INFO_VIEW_MS);
        }
        break;
    case INPUT_HELP:
        force_view_locked(VIEW_HELP, HELP_VIEW_MS);
        break;
    case INPUT_UNKNOWN:
        set_toast_locked("Unknown command");
        break;
    case INPUT_CONFIRM:     // main.c turns this into INPUT_SELECT_TRACK first
    case INPUT_NONE:
    case INPUT_QUIT:
        break;
    }
    render_locked();
    pthread_mutex_unlock(&s_mutex);
}

void lcd_ui_notify(const char *text)
{
    pthread_mutex_lock(&s_mutex);
    if (s_active && text != NULL) {
        set_toast_locked(text);
        render_locked();
    }
    pthread_mutex_unlock(&s_mutex);
}

void lcd_ui_show_message(const char *line1, const char *line2, int duration_ms)
{
    pthread_mutex_lock(&s_mutex);
    if (s_active) {
        snprintf(s_message[0], sizeof(s_message[0]), "%s", line1 != NULL ? line1 : "");
        snprintf(s_message[1], sizeof(s_message[1]), "%s", line2 != NULL ? line2 : "");
        force_view_locked(VIEW_MESSAGE, duration_ms > 0 ? duration_ms : MESSAGE_DEFAULT_MS);
        render_locked();
    }
    pthread_mutex_unlock(&s_mutex);
}

void lcd_ui_refresh(int current_track, const audio_status_t *status)
{
    pthread_mutex_lock(&s_mutex);
    if (s_active) {
        s_track = current_track;
        if (status != NULL) {
            s_status = *status;
        }
        render_locked();
    }
    pthread_mutex_unlock(&s_mutex);
}

void lcd_ui_set_clock(long (*now_ms_function)(void))
{
    pthread_mutex_lock(&s_mutex);
    s_clock = now_ms_function;
    pthread_mutex_unlock(&s_mutex);
}
