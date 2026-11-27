// Host-side test of the GUI: draws every kind of screen into a recording
// canvas (no display needed) and checks that nothing is drawn off-screen,
// that text never runs past the right edge, and that only changes are
// redrawn between frames. Also tests word wrapping.
//
//   ./gui_test                 run the checks
//   ./gui_test --dump DIR      also write each screen's drawing commands to
//                              DIR/<name>.ops (tools/render_preview.py turns
//                              them into PNG previews)
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "app_log.h"
#include "gui.h"
#include "track_list.h"

static int failures = 0;
static int ops = 0;
static int bad_ops = 0;
static FILE *dump = NULL;

// For the background-repair checks
static unsigned char covered[GUI_HEIGHT][GUI_WIDTH];     // painted during the repair cycle
static unsigned char text_shown[GUI_HEIGHT][GUI_WIDTH];  // text cells on screen after the full draw
static bool marking_text = false;
static bool checking_repair = false;
static long erased_text_pixels = 0;

static void mark(int x, int y, int w, int h, bool is_text)
{
    for (int j = y; j < y + h && j < GUI_HEIGHT; j++) {
        for (int i = x; i < x + w && i < GUI_WIDTH; i++) {
            if (i < 0 || j < 0) continue;
            if (marking_text) text_shown[j][i] = is_text;   // later drawing replaces earlier
            if (checking_repair) {
                covered[j][i] = 1;
                if (!is_text && text_shown[j][i]) erased_text_pixels++;   // a fill over visible text = flicker
            }
        }
    }
}

#define CHECK(condition, description) do { \
    int ok_ = (condition); \
    printf("  [%s] %s\n", ok_ ? "PASS" : "FAIL", description); \
    if (!ok_) { printf("         (line %d: %s)\n", __LINE__, #condition); failures++; } } while (0)

static void check_box(int x, int y, int w, int h, const char *what)
{
    if (x < 0 || y < 0 || w < 0 || h < 0 || x + w > GUI_WIDTH || y + h > GUI_HEIGHT) {
        printf("         off-screen %s: x=%d y=%d w=%d h=%d\n", what, x, y, w, h);
        bad_ops++;
    }
}

static void rec_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    ops++;
    check_box(x, y, w, h, "rectangle");
    mark(x, y, w, h, false);
    if (dump) fprintf(dump, "R %d %d %d %d %u\n", x, y, w, h, color);
}

static void rec_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    ops++;
    int xs[3] = {x0, x1, x2}, ys[3] = {y0, y1, y2};
    for (int i = 0; i < 3; i++) check_box(xs[i], ys[i], 0, 0, "triangle corner");
    if (dump) fprintf(dump, "T %d %d %d %d %d %d %u\n", x0, y0, x1, y1, x2, y2, color);
}

static void rec_text(int x, int y, int scale, uint16_t fg, uint16_t bg, const char *text)
{
    ops++;
    check_box(x, y, (int)strlen(text) * CANVAS_CHAR_W(scale), CANVAS_CHAR_H(scale), "text");
    mark(x, y, (int)strlen(text) * CANVAS_CHAR_W(scale), CANVAS_CHAR_H(scale), true);
    if (dump) fprintf(dump, "S %d %d %d %u %u %s\n", x, y, scale, fg, bg, text);
}

static const canvas_t recorder = { GUI_WIDTH, GUI_HEIGHT, rec_fill_rect, rec_fill_triangle, rec_text };

static const char *dump_dir = NULL;

static void render_screen(const char *name, const tour_view_t *view)
{
    if (dump_dir) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s.ops", dump_dir, name);
        dump = fopen(path, "w");
    }
    gui_invalidate();
    bad_ops = 0;
    ops = 0;
    gui_render(view);
    if (dump) {
        fclose(dump);
        dump = NULL;
    }
    char description[128];
    snprintf(description, sizeof(description), "%s: everything on screen (%d drawing commands)", name, ops);
    CHECK(bad_ops == 0, description);
}

// Full draw, then one whole repair cycle: every pixel must be repainted,
// no visible text may be erased (flicker), and afterwards a normal frame
// must find nothing to redraw (the repair kept its bookkeeping right).
static void check_repair(const char *name, const tour_view_t *view)
{
    memset(text_shown, 0, sizeof(text_shown));
    gui_invalidate();
    marking_text = true;
    gui_render(view);
    marking_text = false;

    memset(covered, 0, sizeof(covered));
    erased_text_pixels = 0;
    checking_repair = true;
    int steps = gui_refresh_piece_count(view);
    for (int i = 0; i < steps; i++) {
        gui_refresh_step(view);
    }
    checking_repair = false;

    long missed = 0;
    for (int y = 0; y < GUI_HEIGHT; y++)
        for (int x = 0; x < GUI_WIDTH; x++)
            if (!covered[y][x]) missed++;
    char description[160];
    snprintf(description, sizeof(description), "%s: %d repair steps repaint every pixel (%ld missed)",
             name, steps, missed);
    CHECK(missed == 0, description);
    snprintf(description, sizeof(description), "%s: repair never erases visible text (%ld pixels)",
             name, erased_text_pixels);
    CHECK(erased_text_pixels == 0, description);
    ops = 0;
    gui_render(view);
    snprintf(description, sizeof(description), "%s: after the repair, a normal frame redraws nothing", name);
    CHECK(ops == 0, description);
}

static tour_view_t base_view(void)
{
    tour_view_t v;
    memset(&v, 0, sizeof(v));
    v.mode = TOUR_MODE_CHOOSING;
    v.track_count = track_list_count();
    v.highlighted = 0;
    v.playing = -1;
    v.popup = TOUR_POPUP_NONE;
    v.popup_stop = -1;
    v.audio.state = AUDIO_STOPPED;
    v.audio.volume_percent = 70;
    return v;
}

// ---------------------------------------------------------------------------
static char audio_dir[] = "/tmp/gui_test_XXXXXX";
static const char *names[] = {
    "01_Transportation_Centre", "02_WAC_Bennett", "03_Convo_and_DFA", "04_DAC", "05_AQ",
    "06_ASB", "07_WMC", "08_Lorne_Davies", "09_Terry_Fox",
};
#define NAME_COUNT 9

static void write_file(const char *name, const char *extension, const char *contents)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s%s", audio_dir, name, extension);
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(contents, f);
        fclose(f);
    }
}

static void remove_files(void)
{
    for (int i = 0; i < NAME_COUNT; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s.mp3", audio_dir, names[i]);
        unlink(path);
        snprintf(path, sizeof(path), "%s/%s.txt", audio_dir, names[i]);
        unlink(path);
    }
    rmdir(audio_dir);
}

int main(int argc, char *argv[])
{
    if (argc == 3 && !strcmp(argv[1], "--dump")) {
        dump_dir = argv[2];
        mkdir(dump_dir, 0755);
    }
    app_log_init(NULL, false);
    if (mkdtemp(audio_dir) == NULL) {
        return 2;
    }
    for (int i = 0; i < NAME_COUNT; i++) {
        write_file(names[i], ".mp3", "");
    }
    // Stop 3 has real-looking directions (with Word-style quotes); stop 4 has a very long text.
    write_file(names[2], ".txt",
        "From the WAC Bennett Library, walk east along Convocation Mall \xE2\x80\x94 the "
        "large open courtyard in the centre of campus.\n\nStop beside the Convocation "
        "Mall stage; the Diamond Family Auditorium (\xE2\x80\x9C" "DFA\xE2\x80\x9D) entrance is "
        "on your left.");
    char long_text[1100];
    long_text[0] = '\0';
    for (int i = 0; i < 18; i++) {
        strcat(long_text, "Keep walking straight past the fountain and the stairs. ");
    }
    write_file(names[3], ".txt", long_text);
    if (!track_list_load(audio_dir) || track_list_count() != NAME_COUNT) {
        printf("could not set up the test tracks\n");
        return 2;
    }
    gui_init(&recorder);

    printf("Word wrap\n");
    char lines[8][128];
    int truncated;
    int n = gui_wrap_text("the quick brown fox jumps", 10, &lines[0][0], 128, 8, &truncated);
    CHECK(n == 3 && !strcmp(lines[0], "the quick") && !strcmp(lines[1], "brown fox")
          && !strcmp(lines[2], "jumps") && !truncated, "breaks at spaces");
    n = gui_wrap_text("Transportation", 8, &lines[0][0], 128, 8, &truncated);
    CHECK(n == 2 && !strcmp(lines[0], "Transpor") && !strcmp(lines[1], "tation"), "splits a word longer than a line");
    n = gui_wrap_text("one\n\ntwo", 20, &lines[0][0], 128, 8, &truncated);
    CHECK(n == 3 && !strcmp(lines[1], "") && !strcmp(lines[2], "two"), "keeps blank lines between paragraphs");
    n = gui_wrap_text("a b c d e f", 1, &lines[0][0], 128, 3, &truncated);
    CHECK(n == 3 && truncated, "reports text that does not fit");

    printf("Screens\n");
    tour_view_t v = base_view();
    v.highlighted = 1;
    render_screen("1_choosing", &v);

    v = base_view();
    v.mode = TOUR_MODE_PLAYING;
    v.audio.state = AUDIO_PLAYING;
    v.playing = 4;
    v.highlighted = 7;                      // list scrolled down
    v.audio.position_sec = 161; v.position_sec = 161; v.audio.duration_sec = 307;
    render_screen("2_playing", &v);

    v.mode = TOUR_MODE_PAUSED;
    v.audio.state = AUDIO_PAUSED;
    v.scrubbing = true;
    v.position_sec = 230;                   // previewing a skip
    v.highlighted = 4;
    render_screen("3_paused_skipping", &v);

    v = base_view();
    v.popup = TOUR_POPUP_GUIDE;
    v.popup_stop = 2;
    v.highlighted = 2;
    render_screen("4_directions", &v);

    v.popup_stop = 3;
    v.highlighted = 3;
    render_screen("5_directions_long", &v);

    v.popup_stop = 5;
    v.highlighted = 5;
    render_screen("6_directions_placeholder", &v);

    v.popup = TOUR_POPUP_TOUR_COMPLETE;
    v.popup_stop = -1;
    v.highlighted = 8;
    render_screen("7_tour_complete", &v);

    v = base_view();
    v.mode = TOUR_MODE_PLAYING;
    v.audio.state = AUDIO_PLAYING;
    v.playing = 0;
    v.audio.duration_sec = 3725; v.audio.position_sec = 65; v.position_sec = 65;
    render_screen("8_hour_long_track", &v);

    printf("Background repair (fixes corrupted pixels without flicker)\n");
    v = base_view();
    v.highlighted = 1;
    check_repair("choosing", &v);
    v = base_view();
    v.mode = TOUR_MODE_PLAYING; v.audio.state = AUDIO_PLAYING;
    v.playing = 4; v.highlighted = 7;
    v.audio.position_sec = 161; v.position_sec = 161; v.audio.duration_sec = 307;
    check_repair("playing, list scrolled", &v);
    v.mode = TOUR_MODE_PAUSED; v.audio.state = AUDIO_PAUSED;
    v.scrubbing = true; v.position_sec = 230; v.highlighted = 4;
    check_repair("paused, skipping", &v);
    v = base_view();
    v.mode = TOUR_MODE_PLAYING; v.audio.state = AUDIO_PLAYING; v.playing = 0;
    v.audio.duration_sec = 3725; v.audio.position_sec = 65; v.position_sec = 65;
    check_repair("hour-long track", &v);
    v = base_view();
    v.popup = TOUR_POPUP_GUIDE; v.popup_stop = 2; v.highlighted = 2;
    check_repair("directions popup", &v);
    v.popup_stop = 3; v.highlighted = 3;
    check_repair("long directions popup", &v);
    v.popup = TOUR_POPUP_TOUR_COMPLETE; v.popup_stop = -1;
    check_repair("tour complete popup", &v);

    printf("Only changes are redrawn\n");
    v = base_view();
    v.mode = TOUR_MODE_PLAYING;
    v.audio.state = AUDIO_PLAYING;
    v.playing = 2;
    v.highlighted = 2;
    v.audio.duration_sec = 200; v.audio.position_sec = 50; v.position_sec = 50;
    gui_invalidate();
    gui_render(&v);
    ops = 0;
    gui_render(&v);
    CHECK(ops == 0, "same frame twice: nothing redrawn");
    v.audio.position_sec = 50.4; v.position_sec = 50.4;
    ops = 0;
    gui_render(&v);
    CHECK(ops <= 1, "0.4 s later: at most the bar grows (time text unchanged)");
    v.audio.position_sec = 51; v.position_sec = 51;
    ops = 0;
    gui_render(&v);
    CHECK(ops >= 1 && ops <= 2, "1 s later: bar + time only");
    v.highlighted = 3;
    ops = 0;
    gui_render(&v);
    CHECK(ops > 0 && ops <= 20, "highlight moves: only the two affected rows (about 9 commands each)");
    v.popup = TOUR_POPUP_GUIDE; v.popup_stop = 3;
    gui_render(&v);
    v.popup = TOUR_POPUP_NONE;
    ops = 0;
    gui_render(&v);
    CHECK(ops > 20, "closing a popup redraws the whole screen");

    track_list_cleanup();
    remove_files();
    app_log_cleanup();
    printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
           failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
