// Host-side test of the LCD screens and input mapping, using a fake LCD and a
// fake track list / clock (no hardware, no audio player).
#include <stdio.h>
#include <string.h>
#include "hal/lcd.h"
#include "lcd_ui.h"
#include "track_list.h"

static int failures = 0;

static void check(bool ok, const char *description)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", description);
    if (!ok) {
        failures++;
    }
}

// ---- fake LCD: remembers the last frame ----
static lcd_frame_t s_shown;
static int s_frameCount = 0;
static bool s_lcdStarted = false;

bool lcd_init(void) { s_lcdStarted = true; return true; }
void lcd_cleanup(void) { s_lcdStarted = false; }
void lcd_show_frame(const lcd_frame_t *frame) { s_shown = *frame; s_frameCount++; }

// ---- fake track list ----
static const char *NAMES[] = { "Welcome to SFU", "Library", "Convocation Mall", "Dining Hall", "Pond" };
#define TRACKS ((int)(sizeof(NAMES) / sizeof(NAMES[0])))
bool track_list_load(const char *directory) { (void)directory; return true; }
int track_list_count(void) { return TRACKS; }
const char *track_list_name(int index) { return (index >= 0 && index < TRACKS) ? NAMES[index] : NULL; }
const char *track_list_path(int index) { return track_list_name(index); }
void track_list_cleanup(void) {}

// ---- fake clock ----
static long s_now = 1000;
static long fake_clock(void) { return s_now; }

static bool row_has(int row, const char *text)
{
    return strstr(s_shown.row[row], text) != NULL;
}

static bool any_row_has(const char *text)
{
    for (int r = 0; r < LCD_ROWS; r++) {
        if (row_has(r, text)) {
            return true;
        }
    }
    return false;
}

static audio_status_t status(audio_state_t state, double position, double duration, int volume)
{
    audio_status_t s = { state, position, duration, volume };
    return s;
}

static input_event_t ev(input_type_t type, int value)
{
    input_event_t e = { type, value };
    return e;
}

int main(void)
{
    printf("LCD screens and input mapping\n");
    lcd_ui_set_clock(fake_clock);
    check(lcd_ui_init(), "lcd_ui_init succeeds");
    audio_status_t stopped = status(AUDIO_STOPPED, 0, 0, 70);

    // Frame geometry
    bool geometry = true;
    for (int r = 0; r < LCD_ROWS; r++) {
        geometry = geometry && strlen(s_shown.row[r]) == LCD_COLS;
    }
    check(geometry, "every row is exactly LCD_COLS wide");

    // Stop list
    check(row_has(0, "Select stop 1/5"), "stopped: shows the stop list header");
    check(row_has(1, ">") && row_has(1, "Welcome to SFU"), "first stop is highlighted");

    // Encoder turns
    lcd_ui_move_cursor(1);
    check(lcd_ui_cursor() == 1, "encoder +1 moves the highlight to stop 2");
    check(row_has(0, "Select stop 2/5"), "header follows the highlight");
    lcd_ui_move_cursor(-1);
    lcd_ui_move_cursor(-1);
    check(lcd_ui_cursor() == TRACKS - 1, "turning back from stop 1 wraps to the last stop");
    check(any_row_has(">") && any_row_has("Pond"), "the last stop is visible and highlighted");
    lcd_ui_move_cursor(1);
    check(lcd_ui_cursor() == 0, "turning forward from the last stop wraps to the first");

    // Selection of a stop that is now playing
    lcd_ui_move_cursor(1);                       // cursor = 1 (Library)
    audio_status_t playing = status(AUDIO_PLAYING, 153, 247, 70);
    lcd_ui_on_input(ev(INPUT_SELECT_TRACK, 2), 1, &playing);
    check(row_has(0, "PLAYING 2/5"), "after select: now-playing screen");
    check(row_has(1, "Library"), "now-playing shows the stop name");
    check(row_has(2, "02:33 / 04:07"), "now-playing shows the clock 02:33 / 04:07");
    check(row_has(3, "Playing: Library"), "select: toast confirms the stop");
    s_now += 3000;
    lcd_ui_refresh(1, &playing);
    check(row_has(3, "Vol 70%") && row_has(3, "#######---"), "volume line shows 70% and a bar");

    // Clock follows refresh
    playing = status(AUDIO_PLAYING, 154, 247, 70);
    lcd_ui_refresh(1, &playing);
    check(row_has(2, "02:34 / 04:07"), "refresh advances the clock");

    // Pause
    audio_status_t paused = status(AUDIO_PAUSED, 154, 247, 70);
    lcd_ui_on_input(ev(INPUT_TOGGLE_PAUSE, 0), 1, &paused);
    check(row_has(0, "PAUSED 2/5"), "pause: header says PAUSED");
    check(row_has(3, "Paused"), "pause: toast says Paused");
    s_now += 3000;
    lcd_ui_refresh(1, &paused);
    check(row_has(3, "Vol 70%"), "toast expires and the volume line returns");

    // Volume
    audio_status_t louder = status(AUDIO_PLAYING, 160, 247, 80);
    lcd_ui_on_input(ev(INPUT_VOLUME_UP, 0), 1, &louder);
    check(row_has(3, "Volume 80%"), "volume up: toast shows Volume 80%");

    // Skip
    lcd_ui_on_input(ev(INPUT_SEEK_FORWARD, 0), 1, &louder);
    check(row_has(3, "Skipped forward"), "skip forward is shown");

    // Browsing while playing, then returning
    lcd_ui_move_cursor(1);
    check(row_has(0, "Select stop 3/5"), "encoder turn while playing shows the list");
    check(any_row_has("*") , "the playing stop is marked with *");
    s_now += 6000;
    lcd_ui_refresh(1, &louder);
    check(row_has(0, "PLAYING 2/5"), "after a few seconds it returns to now-playing");

    // Selection errors
    lcd_ui_on_input(ev(INPUT_SELECT_TRACK, 99), 1, &louder);
    check(row_has(3, "No track 99"), "unknown track number is reported");

    // Stop
    lcd_ui_on_input(ev(INPUT_STOP, 0), 1, &stopped);
    check(row_has(0, "Stopped") || row_has(0, "Select stop"), "stop: back to the list");
    check(row_has(0, "Stopped"), "stop: toast says Stopped");

    // Nothing playing
    s_now += 3000;
    lcd_ui_on_input(ev(INPUT_TOGGLE_PAUSE, 0), 1, &stopped);
    check(row_has(0, "Nothing playing"), "pause with nothing playing is reported");

    // Help
    lcd_ui_on_input(ev(INPUT_HELP, 0), 1, &stopped);
    check(row_has(0, "Turn: pick a stop"), "help screen is shown");
    s_now += 7000;
    lcd_ui_refresh(1, &stopped);
    check(row_has(0, "Select stop"), "help screen times out");

    // Welcome message (for boot)
    lcd_ui_show_message("Welcome to", "SFU Campus Tour", 3000);
    check(any_row_has("Welcome to") && any_row_has("SFU Campus Tour"), "boot message is shown");
    s_now += 3500;
    lcd_ui_refresh(-1, &stopped);
    check(row_has(0, "Select stop"), "boot message goes away after its time");

    // Unchanged screens are not redrawn
    int before = s_frameCount;
    lcd_ui_refresh(-1, &stopped);
    lcd_ui_refresh(-1, &stopped);
    check(s_frameCount == before, "identical frames are not resent to the LCD");

    // Text longer than the screen is cut, not overflowed
    lcd_ui_notify("This message is much much longer than the screen");
    check(strlen(s_shown.row[0]) == LCD_COLS, "long text is cut to the width");

    lcd_ui_cleanup();
    check(!s_lcdStarted, "cleanup releases the LCD");
    int afterCleanup = s_frameCount;
    lcd_ui_move_cursor(1);
    check(s_frameCount == afterCleanup, "calls after cleanup do nothing");

    printf("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
