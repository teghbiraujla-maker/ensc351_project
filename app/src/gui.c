#include "gui.h"
#include <stdio.h>
#include <string.h>
#include "guide_text.h"
#include "playback_display.h"
#include "track_list.h"

// ---------------------------------------------------------------------------
// How drawing works
//
// The screen is split into PIECES that never overlap and together cover all
// 800 x 480 pixels. Each piece is painted without ever erasing what is
// visible: text is drawn with its own background colour, and only the empty
// space around it is filled. So repainting a piece with the same content
// shows no flicker at all.
//
// That is what lets gui_refresh_step() repaint one piece every
// GUI_REFRESH_STEP_MS: any pixels corrupted by a bad SPI transfer are put
// right within one cycle, invisibly. Normal updates (a row changing, the bar
// growing, a popup opening) use the same painters.
// ---------------------------------------------------------------------------

// Colours (RGB565)
#define COLOR_BG       CANVAS_RGB(14, 18, 26)     // near-black navy
#define COLOR_PANEL    CANVAS_RGB(32, 38, 52)     // popup background
#define COLOR_DIVIDER  CANVAS_RGB(56, 64, 80)
#define COLOR_TEXT     CANVAS_RGB(255, 255, 255)
#define COLOR_DIM      CANVAS_RGB(150, 160, 178)
#define COLOR_ACCENT   CANVAS_RGB(166, 25, 46)    // SFU red: highlight, progress
#define COLOR_BAR_BG   CANVAS_RGB(60, 68, 86)
#define COLOR_AMBER    CANVAS_RGB(245, 180, 40)   // playing marker, skip preview

// ---- Layout (pixels). Top area y 0..319, bottom area y 320..479 ----
#define TOP_HEIGHT     320
#define LEFT_WIDTH     400                   // left panel x 0..399
#define DIVIDER_X      400                   // divider column x 400..401
#define DIVIDER_W      2
#define DIVIDER_TOP    12
#define DIVIDER_H      (TOP_HEIGHT - 24)

#define LEFT_X         24
#define LABEL_Y        20
#define NAME_Y         48
#define NAME_SCALE     2                     // 24 x 48 characters
#define NAME_CHARS     15                    // (400 - 2*24) / 24
#define NAME_LINES     2
#define HINT_Y         196
#define HINT_LINE_H    18

#define RIGHT_X        402                   // right panel x 402..799
#define LIST_X         404                   // rows x 404..789
#define LIST_Y         8
#define LIST_W         386
#define ROW_H          50
#define ROW_FILL_H     46
#define VISIBLE_ROWS   6
#define LIST_SCALE     1                     // 16 x 32 characters
#define ROW_TEXT_DY    ((ROW_FILL_H - 32) / 2)
#define NUMBER_X       412
#define NUMBER_CHARS   2
#define ITEM_X         452
#define ITEM_CHARS     21                    // (790 - 452) / 16
#define SCROLL_X       794                   // scrollbar x 794..797
#define SCROLL_W       4
#define SCROLL_H       (VISIBLE_ROWS * ROW_H - 4)
#define LIST_BOTTOM    (LIST_Y + SCROLL_H)   // 304

#define BOTTOM_Y       320                   // divider y 320..321, strip to 335
#define BAR_X          20
#define BAR_Y          336
#define BAR_W          760
#define BAR_H          30
#define ROW2_TOP       (BAR_Y + BAR_H)       // 366: time / icon / volume pieces start
#define ROW2_Y         392                   // time / volume text band (48 px tall)
#define ROW2_H         48
#define TIME_X         24
#define TIME_FIELD     13                    // characters reserved at scale 2
#define ICON_LEFT      368                   // icon piece x 368..431
#define ICON_W         64
#define ICON_CX        400
#define ICON_Y         388
#define ICON_SIZE      48
#define ICON_BOX_TOP   384
#define ICON_BOX_H     56
#define STATE_LABEL_Y  446
#define VOLUME_LEFT    (ICON_LEFT + ICON_W)  // volume piece x 432..799
#define VOLUME_RIGHT   776

#define POPUP_X        24
#define POPUP_Y        24
#define POPUP_W        752
#define POPUP_H        432
#define POPUP_BORDER   4
#define POPUP_TEXT_X   56
#define POPUP_TEXT_RIGHT (POPUP_X + POPUP_W - 32)
#define POPUP_BODY_Y   148
#define POPUP_BODY_BOTTOM 400
#define POPUP_FOOTER_Y 420

#define MAX_LINES      16
#define LINE_SIZE      128
#define MAX_ITEMS      24

#define MAIN_PIECES    18
#define HINTS_PER_PIECE 2

static const canvas_t *canvas = NULL;

// What is on the screen now, so normal updates only redraw changes.
static struct {
    bool valid;                    // false: redraw everything next frame
    tour_popup_t popup;
    int popup_stop;
    int left_playing;
    int first_visible;
    int row_track[VISIBLE_ROWS];
    bool row_highlighted[VISIBLE_ROWS];
    bool row_playing[VISIBLE_ROWS];
    int bar_px;
    bool bar_scrubbing;
    char time_text[32];
    int time_scale;
    bool time_scrubbing;
    tour_mode_t icon_mode;
    char volume_text[16];
} drawn;

// Background repair position
static int refresh_piece = 0;
static tour_popup_t refresh_popup = TOUR_POPUP_NONE;
static int refresh_popup_stop = -1;

void gui_init(const canvas_t *new_canvas)
{
    canvas = new_canvas;
    gui_invalidate();
}

void gui_invalidate(void)
{
    drawn.valid = false;
    drawn.popup = TOUR_POPUP_NONE;
    drawn.popup_stop = -1;
    refresh_piece = 0;
}

static void fill(int x, int y, int w, int h, uint16_t color)
{
    if (w > 0 && h > 0) {
        canvas->fill_rect(x, y, w, h, color);
    }
}

// ---------------------------------------------------------------------------
// Word wrap
// ---------------------------------------------------------------------------
int gui_wrap_text(const char *text, int width_chars, char *lines, int line_size,
                  int max_lines, int *truncated)
{
    int count = 0;
    char current[LINE_SIZE];
    int length = 0;
    *truncated = 0;
    if (width_chars > LINE_SIZE - 1) width_chars = LINE_SIZE - 1;

#define EMIT() do { \
        if (count >= max_lines) { *truncated = 1; return count; } \
        current[length] = '\0'; \
        snprintf(lines + count * line_size, (size_t)line_size, "%s", current); \
        count++; length = 0; } while (0)

    const char *p = text;
    while (*p != '\0') {
        if (*p == '\n') {
            EMIT();
            p++;
            continue;
        }
        if (*p == ' ') {
            p++;
            continue;
        }
        const char *word = p;
        while (*p != '\0' && *p != ' ' && *p != '\n') {
            p++;
        }
        int word_length = (int)(p - word);
        while (word_length > 0) {
            int needed = (length > 0) ? length + 1 + word_length : word_length;
            if (needed <= width_chars) {
                if (length > 0) current[length++] = ' ';
                memcpy(current + length, word, (size_t)word_length);
                length += word_length;
                word_length = 0;
            } else if (length > 0) {
                EMIT();                                   // word goes on the next line
            } else {
                memcpy(current, word, (size_t)width_chars);   // word longer than a line: split it
                length = width_chars;
                word += width_chars;
                word_length -= width_chars;
                EMIT();
            }
        }
    }
    if (length > 0) {
        EMIT();
    }
#undef EMIT
    return count;
}

// ---------------------------------------------------------------------------
// Text panels: a rectangle with lines of text (and thin rules) stacked from
// top to bottom. Painting fills only the space around each line, so a panel
// can be repainted with the same content without anything disappearing.
// ---------------------------------------------------------------------------
typedef struct {
    bool rule;                 // a filled line (w x h) instead of text
    int x, y, w, h;
    int scale;
    uint16_t fg;
    char text[LINE_SIZE];
} item_t;

typedef struct {
    int x, y, w, h;
    uint16_t bg;
    int count;
    item_t items[MAX_ITEMS];
} panel_t;

static void panel_start(panel_t *p, int x, int y, int w, int h, uint16_t bg)
{
    p->x = x; p->y = y; p->w = w; p->h = h; p->bg = bg;
    p->count = 0;
}

// Add a line of text (cut to fit the panel). Lines must be added top to bottom.
static void panel_text(panel_t *p, int x, int y, int scale, uint16_t fg, const char *text)
{
    if (p->count >= MAX_ITEMS) return;
    item_t *it = &p->items[p->count++];
    it->rule = false;
    it->x = x; it->y = y; it->scale = scale; it->fg = fg;
    int fit = (p->x + p->w - x) / CANVAS_CHAR_W(scale);
    if (fit > LINE_SIZE - 1) fit = LINE_SIZE - 1;
    if (fit < 0) fit = 0;
    snprintf(it->text, sizeof(it->text), "%.*s", fit, text);
    it->w = (int)strlen(it->text) * CANVAS_CHAR_W(scale);
    it->h = CANVAS_CHAR_H(scale);
}

static void panel_text_centered(panel_t *p, int center_x, int y, int scale, uint16_t fg, const char *text)
{
    int width = (int)strlen(text) * CANVAS_CHAR_W(scale);
    panel_text(p, center_x - width / 2, y, scale, fg, text);
}

static void panel_rule(panel_t *p, int x, int y, int w, int h, uint16_t color)
{
    if (p->count >= MAX_ITEMS) return;
    item_t *it = &p->items[p->count++];
    it->rule = true;
    it->x = x; it->y = y; it->w = w; it->h = h; it->fg = color;
    it->text[0] = '\0';
}

// Wrapped text, one item per line; the last line ends in "..." if it did not fit.
static int panel_wrapped(panel_t *p, int x, int y, int scale, int width_chars, int max_lines,
                         int line_height, uint16_t fg, const char *text, int *truncated_out)
{
    char lines[MAX_LINES][LINE_SIZE];
    int truncated = 0;
    if (max_lines > MAX_LINES) max_lines = MAX_LINES;
    int count = gui_wrap_text(text, width_chars, &lines[0][0], LINE_SIZE, max_lines, &truncated);
    if (truncated && count > 0) {
        char *last = lines[count - 1];
        size_t len = strlen(last);
        size_t keep = ((size_t)width_chars >= 3 && len > (size_t)width_chars - 3) ? (size_t)width_chars - 3 : len;
        snprintf(last + keep, LINE_SIZE - keep, "...");
    }
    for (int i = 0; i < count; i++) {
        panel_text(p, x, y + i * line_height, scale, fg, lines[i]);
    }
    if (truncated_out) *truncated_out = truncated;
    return count;
}

// Paint items [first, last) and the gaps above them; `tail` also fills
// below the last one. Calling it for consecutive ranges paints the whole panel.
static void panel_paint(const panel_t *p, int first, int last, bool tail)
{
    int previous_bottom = (first == 0) ? p->y : p->items[first - 1].y + p->items[first - 1].h;
    int right = p->x + p->w;
    for (int k = first; k < last; k++) {
        const item_t *it = &p->items[k];
        fill(p->x, previous_bottom, p->w, it->y - previous_bottom, p->bg);   // gap above
        fill(p->x, it->y, it->x - p->x, it->h, p->bg);                       // left of the line
        if (it->rule) {
            fill(it->x, it->y, it->w, it->h, it->fg);
        } else if (it->text[0] != '\0') {
            canvas->text(it->x, it->y, it->scale, it->fg, p->bg, it->text);
        }
        fill(it->x + it->w, it->y, right - (it->x + it->w), it->h, p->bg);  // right of the line
        previous_bottom = it->y + it->h;
    }
    if (tail) {
        fill(p->x, previous_bottom, p->w, p->y + p->h - previous_bottom, p->bg);
    }
}

// ---------------------------------------------------------------------------
// Left panel: now playing + controls reminder
// ---------------------------------------------------------------------------
static int build_left_panel(const tour_view_t *view, panel_t *p)
{
    panel_start(p, 0, 0, LEFT_WIDTH, TOP_HEIGHT, COLOR_BG);
    char label[64];
    if (view->playing >= 0) {
        snprintf(label, sizeof(label), "NOW PLAYING - STOP %d OF %d", view->playing + 1, view->track_count);
        panel_text(p, LEFT_X, LABEL_Y, 0, COLOR_DIM, label);
        panel_wrapped(p, LEFT_X, NAME_Y, NAME_SCALE, NAME_CHARS, NAME_LINES,
                      CANVAS_CHAR_H(NAME_SCALE) + 4, COLOR_TEXT, track_list_name(view->playing), NULL);
    } else {
        panel_text(p, LEFT_X, LABEL_Y, 0, COLOR_DIM, "NOTHING PLAYING");
        panel_wrapped(p, LEFT_X, NAME_Y, 1, 22, 3, CANVAS_CHAR_H(1) + 8, COLOR_DIM,
                      "Turn the knob to choose a stop, then press it to play.", NULL);
    }
    int split = p->count;            // repair piece 1 = label + name, piece 2 = hints
    static const char *hints[] = {
        "KNOB   turn    choose a stop",
        "       press   play / pause",
        "       hold    end the track",
        "STICK  up/down volume",
        "       left/rt skip back/forward",
        "       press   directions",
    };
    for (int i = 0; i < (int)(sizeof(hints) / sizeof(hints[0])); i++) {
        panel_text(p, LEFT_X, HINT_Y + i * HINT_LINE_H, 0, COLOR_DIM, hints[i]);
    }
    return split;
}

// part 0 = everything; part 1 = label + name; parts 2, 3, 4 = two hint lines each
// (the small font is slow to draw, so the hints are split to keep each repair step short).
static void paint_left_panel(const tour_view_t *view, int part)
{
    panel_t p;
    int split = build_left_panel(view, &p);
    if (part == 0) {
        panel_paint(&p, 0, p.count, true);
    } else if (part == 1) {
        panel_paint(&p, 0, split, false);
    } else {
        int first = split + (part - 2) * HINTS_PER_PIECE;
        int last = first + HINTS_PER_PIECE;
        bool final_part = (part == 4);
        if (final_part || last > p.count) last = p.count;
        if (first < last || final_part) {
            panel_paint(&p, first < p.count ? first : p.count, last, final_part);
        }
    }
    drawn.left_playing = view->playing;
}

static void paint_divider_column(void)
{
    fill(DIVIDER_X, 0, DIVIDER_W, DIVIDER_TOP, COLOR_BG);
    fill(DIVIDER_X, DIVIDER_TOP, DIVIDER_W, DIVIDER_H, COLOR_DIVIDER);
    fill(DIVIDER_X, DIVIDER_TOP + DIVIDER_H, DIVIDER_W, TOP_HEIGHT - DIVIDER_TOP - DIVIDER_H, COLOR_BG);
}

// ---------------------------------------------------------------------------
// Track list (right panel)
// ---------------------------------------------------------------------------
static int choose_first_visible(const tour_view_t *view, int previous_first)
{
    int first = (previous_first >= 0) ? previous_first : 0;
    if (view->highlighted >= 0) {
        if (view->highlighted < first) first = view->highlighted;
        if (view->highlighted >= first + VISIBLE_ROWS) first = view->highlighted - VISIBLE_ROWS + 1;
    }
    int last_first = view->track_count - VISIBLE_ROWS;
    if (last_first < 0) last_first = 0;
    if (first > last_first) first = last_first;
    if (first < 0) first = 0;
    return first;
}

// One row, x 404..789, 46 px tall, painted in strips around its text.
static void paint_row(int row, int track, bool highlighted, bool playing, int track_count)
{
    int y = LIST_Y + row * ROW_H;
    uint16_t bg = highlighted ? COLOR_ACCENT : COLOR_BG;
    const char *name = track_list_name(track);
    int text_y = y + ROW_TEXT_DY;
    int text_h = CANVAS_CHAR_H(LIST_SCALE);
    int right = LIST_X + LIST_W;

    if (name == NULL) {                                // below the last stop (or no tracks)
        if (row == 0 && track_count == 0) {
            fill(LIST_X, y, LIST_W, ROW_TEXT_DY, COLOR_BG);
            const char *message = "No recordings found.";
            canvas->text(LIST_X + 8, text_y, LIST_SCALE, COLOR_DIM, COLOR_BG, message);
            int end = LIST_X + 8 + (int)strlen(message) * CANVAS_CHAR_W(LIST_SCALE);
            fill(LIST_X, text_y, 8, text_h, COLOR_BG);
            fill(end, text_y, right - end, text_h, COLOR_BG);
            fill(LIST_X, text_y + text_h, LIST_W, ROW_FILL_H - ROW_TEXT_DY - text_h, COLOR_BG);
        } else {
            fill(LIST_X, y, LIST_W, ROW_FILL_H, COLOR_BG);
        }
        return;
    }
    fill(LIST_X, y, LIST_W, ROW_TEXT_DY, bg);                              // strip above the text
    fill(LIST_X, text_y, NUMBER_X - LIST_X, text_h, bg);                   // left margin
    int number_end = NUMBER_X + NUMBER_CHARS * CANVAS_CHAR_W(LIST_SCALE);
    if (playing) {
        // Amber "play" triangle instead of the number
        fill(NUMBER_X, text_y, number_end - NUMBER_X, text_h, bg);
        int top = y + 11, bottom = y + ROW_FILL_H - 11;
        canvas->fill_triangle(NUMBER_X + 4, top, NUMBER_X + 4, bottom,
                              NUMBER_X + 28, (top + bottom) / 2, COLOR_AMBER);
    } else {
        char number[16];
        snprintf(number, sizeof(number), "%-*d", NUMBER_CHARS, track + 1);
        number[NUMBER_CHARS] = '\0';
        canvas->text(NUMBER_X, text_y, LIST_SCALE, highlighted ? COLOR_TEXT : COLOR_DIM, bg, number);
    }
    fill(number_end, text_y, ITEM_X - number_end, text_h, bg);             // between number and name
    char item[ITEM_CHARS + 1];
    snprintf(item, sizeof(item), "%s", name);                              // cut to fit the row
    canvas->text(ITEM_X, text_y, LIST_SCALE, COLOR_TEXT, bg, item);
    int item_end = ITEM_X + (int)strlen(item) * CANVAS_CHAR_W(LIST_SCALE);
    fill(item_end, text_y, right - item_end, text_h, bg);                  // right of the name
    fill(LIST_X, text_y + text_h, LIST_W, ROW_FILL_H - ROW_TEXT_DY - text_h, bg);   // strip below
}

static void paint_list_row(const tour_view_t *view, int row)
{
    int track = drawn.first_visible + row;
    bool highlighted = (track == view->highlighted);
    bool playing = (track == view->playing);
    paint_row(row, track, highlighted, playing, view->track_count);
    drawn.row_track[row] = track;
    drawn.row_highlighted[row] = highlighted;
    drawn.row_playing[row] = playing;
}

// Everything in the right panel that is not a row or the scrollbar.
static void paint_list_frame(void)
{
    fill(RIGHT_X, 0, LIST_X - RIGHT_X, TOP_HEIGHT, COLOR_BG);                   // x 402..403
    fill(LIST_X + LIST_W, 0, SCROLL_X - (LIST_X + LIST_W), TOP_HEIGHT, COLOR_BG); // x 790..793
    fill(SCROLL_X + SCROLL_W, 0, GUI_WIDTH - (SCROLL_X + SCROLL_W), TOP_HEIGHT, COLOR_BG); // x 798..799
    fill(LIST_X, 0, LIST_W, LIST_Y, COLOR_BG);                                 // above the first row
    for (int row = 0; row < VISIBLE_ROWS - 1; row++) {                         // gaps between rows
        fill(LIST_X, LIST_Y + row * ROW_H + ROW_FILL_H, LIST_W, ROW_H - ROW_FILL_H, COLOR_BG);
    }
    fill(LIST_X, LIST_BOTTOM, LIST_W, TOP_HEIGHT - LIST_BOTTOM, COLOR_BG);     // below the last row
}

static void paint_scrollbar(const tour_view_t *view)
{
    fill(SCROLL_X, 0, SCROLL_W, LIST_Y, COLOR_BG);
    if (view->track_count <= VISIBLE_ROWS) {
        fill(SCROLL_X, LIST_Y, SCROLL_W, SCROLL_H, COLOR_BG);
    } else {
        // thumb height = track height x visible / total;  thumb top = height x first / total
        int thumb_h = SCROLL_H * VISIBLE_ROWS / view->track_count;
        int thumb_y = LIST_Y + SCROLL_H * drawn.first_visible / view->track_count;
        fill(SCROLL_X, LIST_Y, SCROLL_W, thumb_y - LIST_Y, COLOR_BAR_BG);
        fill(SCROLL_X, thumb_y, SCROLL_W, thumb_h, COLOR_DIM);
        fill(SCROLL_X, thumb_y + thumb_h, SCROLL_W, LIST_BOTTOM - (thumb_y + thumb_h), COLOR_BAR_BG);
    }
    fill(SCROLL_X, LIST_BOTTOM, SCROLL_W, TOP_HEIGHT - LIST_BOTTOM, COLOR_BG);
}

static void update_list(const tour_view_t *view)
{
    int first = choose_first_visible(view, drawn.valid ? drawn.first_visible : -1);
    bool scrolled = !drawn.valid || first != drawn.first_visible;
    drawn.first_visible = first;
    for (int row = 0; row < VISIBLE_ROWS; row++) {
        int track = first + row;
        if (scrolled || drawn.row_track[row] != track ||
            drawn.row_highlighted[row] != (track == view->highlighted) ||
            drawn.row_playing[row] != (track == view->playing)) {
            paint_list_row(view, row);
        }
    }
    if (scrolled) {
        paint_scrollbar(view);
    }
}

// ---------------------------------------------------------------------------
// Bottom: progress bar, time, play/pause indicator, volume
// ---------------------------------------------------------------------------
static void paint_bottom_frame(void)
{
    fill(0, BOTTOM_Y, GUI_WIDTH, 2, COLOR_DIVIDER);
    fill(0, BOTTOM_Y + 2, GUI_WIDTH, BAR_Y - BOTTOM_Y - 2, COLOR_BG);
}

static int bar_pixels(const tour_view_t *view)
{
    double fraction = 0.0;
    if (view->mode != TOUR_MODE_CHOOSING && view->audio.duration_sec > 0.0) {
        fraction = view->position_sec / view->audio.duration_sec;
        if (fraction < 0.0) fraction = 0.0;
        if (fraction > 1.0) fraction = 1.0;
    }
    return (int)(fraction * BAR_W + 0.5);       // filled width = bar width x fraction (rounded)
}

static void paint_bar(const tour_view_t *view)   // the whole band y 336..365
{
    int px = bar_pixels(view);
    uint16_t color = view->scrubbing ? COLOR_AMBER : COLOR_ACCENT;
    fill(0, BAR_Y, BAR_X, BAR_H, COLOR_BG);
    fill(BAR_X, BAR_Y, px, BAR_H, color);
    fill(BAR_X + px, BAR_Y, BAR_W - px, BAR_H, COLOR_BAR_BG);
    fill(BAR_X + BAR_W, BAR_Y, GUI_WIDTH - (BAR_X + BAR_W), BAR_H, COLOR_BG);
    drawn.bar_px = px;
    drawn.bar_scrubbing = view->scrubbing;
}

static void update_bar(const tour_view_t *view)
{
    int px = bar_pixels(view);
    uint16_t color = view->scrubbing ? COLOR_AMBER : COLOR_ACCENT;
    if (!drawn.valid || drawn.bar_scrubbing != view->scrubbing) {
        paint_bar(view);
        return;
    }
    if (px > drawn.bar_px) {
        fill(BAR_X + drawn.bar_px, BAR_Y, px - drawn.bar_px, BAR_H, color);          // grew
    } else if (px < drawn.bar_px) {
        fill(BAR_X + px, BAR_Y, drawn.bar_px - px, BAR_H, COLOR_BAR_BG);             // shrank
    }
    drawn.bar_px = px;
}

static void time_text_for(const tour_view_t *view, char *text, size_t size, int *scale, char *padded, size_t padded_size)
{
    if (view->mode == TOUR_MODE_CHOOSING) {
        snprintf(text, size, "-:--/-:--");
    } else {
        playback_display_time(view->position_sec, view->audio.duration_sec, text, size);
    }
    *scale = ((int)strlen(text) <= TIME_FIELD) ? 2 : 1;      // hour-long tracks use smaller text
    int field = (*scale == 2) ? TIME_FIELD : TIME_FIELD * 3 / 2;
    snprintf(padded, padded_size, "%-*s", field, text);       // spaces erase longer old text
}

static void paint_time(const tour_view_t *view)   // the piece x 0..367, y 366..479
{
    char text[32], padded[40];
    int scale;
    time_text_for(view, text, sizeof(text), &scale, padded, sizeof(padded));
    uint16_t fg = view->scrubbing ? COLOR_AMBER : COLOR_TEXT;
    int text_h = CANVAS_CHAR_H(scale);
    int text_y = ROW2_Y + (ROW2_H - text_h) / 2;
    int text_end = TIME_X + (int)strlen(padded) * CANVAS_CHAR_W(scale);

    fill(0, ROW2_TOP, ICON_LEFT, ROW2_Y - ROW2_TOP, COLOR_BG);
    fill(0, ROW2_Y, TIME_X, ROW2_H, COLOR_BG);
    fill(TIME_X, ROW2_Y, text_end - TIME_X, text_y - ROW2_Y, COLOR_BG);              // only for small text
    canvas->text(TIME_X, text_y, scale, fg, COLOR_BG, padded);
    fill(TIME_X, text_y + text_h, text_end - TIME_X, ROW2_Y + ROW2_H - (text_y + text_h), COLOR_BG);
    fill(text_end, ROW2_Y, ICON_LEFT - text_end, ROW2_H, COLOR_BG);
    fill(0, ROW2_Y + ROW2_H, ICON_LEFT, GUI_HEIGHT - (ROW2_Y + ROW2_H), COLOR_BG);

    snprintf(drawn.time_text, sizeof(drawn.time_text), "%s", text);
    drawn.time_scale = scale;
    drawn.time_scrubbing = view->scrubbing;
}

static void update_time(const tour_view_t *view)
{
    char text[32], padded[40];
    int scale;
    time_text_for(view, text, sizeof(text), &scale, padded, sizeof(padded));
    if (!drawn.valid || drawn.time_scale != scale) {
        paint_time(view);
        return;
    }
    if (drawn.time_scrubbing == view->scrubbing && strcmp(drawn.time_text, text) == 0) {
        return;
    }
    int y = ROW2_Y + (ROW2_H - CANVAS_CHAR_H(scale)) / 2;
    canvas->text(TIME_X, y, scale, view->scrubbing ? COLOR_AMBER : COLOR_TEXT, COLOR_BG, padded);
    snprintf(drawn.time_text, sizeof(drawn.time_text), "%s", text);
    drawn.time_scrubbing = view->scrubbing;
}

static void paint_indicator(const tour_view_t *view)   // the piece x 368..431, y 366..479
{
    fill(ICON_LEFT, ROW2_TOP, ICON_W, ICON_BOX_TOP - ROW2_TOP, COLOR_BG);
    fill(ICON_LEFT, ICON_BOX_TOP, ICON_W, ICON_BOX_H, COLOR_BG);     // the shape is small: no visible flicker
    int top = ICON_Y, bottom = ICON_Y + ICON_SIZE;
    const char *label;
    if (view->mode == TOUR_MODE_PLAYING) {
        canvas->fill_triangle(ICON_CX - 18, top, ICON_CX - 18, bottom, ICON_CX + 24, (top + bottom) / 2, COLOR_TEXT);
        label = "PLAYING";
    } else if (view->mode == TOUR_MODE_PAUSED) {
        fill(ICON_CX - 20, top, 14, ICON_SIZE, COLOR_TEXT);
        fill(ICON_CX + 6, top, 14, ICON_SIZE, COLOR_TEXT);
        label = "PAUSED";
    } else {
        fill(ICON_CX - 18, top + 6, 36, 36, COLOR_DIM);
        label = "STOPPED";
    }
    int label_top = ICON_BOX_TOP + ICON_BOX_H;
    fill(ICON_LEFT, label_top, ICON_W, STATE_LABEL_Y - label_top, COLOR_BG);
    int label_w = (int)strlen(label) * CANVAS_CHAR_W(0);
    int label_x = ICON_CX - label_w / 2;
    fill(ICON_LEFT, STATE_LABEL_Y, label_x - ICON_LEFT, CANVAS_CHAR_H(0), COLOR_BG);
    canvas->text(label_x, STATE_LABEL_Y, 0, COLOR_DIM, COLOR_BG, label);
    fill(label_x + label_w, STATE_LABEL_Y, ICON_LEFT + ICON_W - (label_x + label_w), CANVAS_CHAR_H(0), COLOR_BG);
    int below = STATE_LABEL_Y + CANVAS_CHAR_H(0);
    fill(ICON_LEFT, below, ICON_W, GUI_HEIGHT - below, COLOR_BG);
    drawn.icon_mode = view->mode;
}

static void volume_text_for(const tour_view_t *view, char *text, size_t size)
{
    char label[16];
    snprintf(label, sizeof(label), "VOL %d%%", view->audio.volume_percent);
    snprintf(text, size, "%8s", label);          // right-aligned, fixed 8 characters (spaces erase old text)
}

static void paint_volume(const tour_view_t *view)   // the piece x 432..799, y 366..479
{
    char text[16];
    volume_text_for(view, text, sizeof(text));
    int width = (int)strlen(text) * CANVAS_CHAR_W(2);
    int text_x = VOLUME_RIGHT - width;
    fill(VOLUME_LEFT, ROW2_TOP, GUI_WIDTH - VOLUME_LEFT, ROW2_Y - ROW2_TOP, COLOR_BG);
    fill(VOLUME_LEFT, ROW2_Y, text_x - VOLUME_LEFT, ROW2_H, COLOR_BG);
    canvas->text(text_x, ROW2_Y, 2, COLOR_TEXT, COLOR_BG, text);
    fill(VOLUME_RIGHT, ROW2_Y, GUI_WIDTH - VOLUME_RIGHT, ROW2_H, COLOR_BG);
    fill(VOLUME_LEFT, ROW2_Y + ROW2_H, GUI_WIDTH - VOLUME_LEFT, GUI_HEIGHT - (ROW2_Y + ROW2_H), COLOR_BG);
    snprintf(drawn.volume_text, sizeof(drawn.volume_text), "%s", text);
}

static void update_volume(const tour_view_t *view)
{
    char text[16];
    volume_text_for(view, text, sizeof(text));
    if (!drawn.valid) {
        paint_volume(view);
        return;
    }
    if (strcmp(drawn.volume_text, text) == 0) {
        return;
    }
    int width = (int)strlen(text) * CANVAS_CHAR_W(2);
    canvas->text(VOLUME_RIGHT - width, ROW2_Y, 2, COLOR_TEXT, COLOR_BG, text);
    snprintf(drawn.volume_text, sizeof(drawn.volume_text), "%s", text);
}

// One piece of the main screen (0 .. MAIN_PIECES-1). Together they cover every pixel.
static void paint_main_piece(const tour_view_t *view, int piece)
{
    switch (piece) {
    case 0:  paint_left_panel(view, 1); break;
    case 1:  paint_left_panel(view, 2); break;
    case 2:  paint_left_panel(view, 3); break;
    case 3:  paint_left_panel(view, 4); break;
    case 4:  paint_divider_column(); break;
    case 5: case 6: case 7: case 8: case 9: case 10:
             paint_list_row(view, piece - 5); break;
    case 11: paint_list_frame(); break;
    case 12: paint_scrollbar(view); break;
    case 13: paint_bottom_frame(); break;
    case 14: paint_bar(view); break;
    case 15: paint_time(view); break;
    case 16: paint_indicator(view); break;
    default: paint_volume(view); break;
    }
}

// ---------------------------------------------------------------------------
// Popups: an outer frame (margins + red border) and an inner text panel.
// ---------------------------------------------------------------------------
static void paint_popup_frame(void)
{
    fill(0, 0, GUI_WIDTH, POPUP_Y, COLOR_BG);                                       // margins
    fill(0, POPUP_Y + POPUP_H, GUI_WIDTH, GUI_HEIGHT - (POPUP_Y + POPUP_H), COLOR_BG);
    fill(0, POPUP_Y, POPUP_X, POPUP_H, COLOR_BG);
    fill(POPUP_X + POPUP_W, POPUP_Y, GUI_WIDTH - (POPUP_X + POPUP_W), POPUP_H, COLOR_BG);
    fill(POPUP_X, POPUP_Y, POPUP_W, POPUP_BORDER, COLOR_ACCENT);                   // border
    fill(POPUP_X, POPUP_Y + POPUP_H - POPUP_BORDER, POPUP_W, POPUP_BORDER, COLOR_ACCENT);
    fill(POPUP_X, POPUP_Y + POPUP_BORDER, POPUP_BORDER, POPUP_H - 2 * POPUP_BORDER, COLOR_ACCENT);
    fill(POPUP_X + POPUP_W - POPUP_BORDER, POPUP_Y + POPUP_BORDER, POPUP_BORDER,
         POPUP_H - 2 * POPUP_BORDER, COLOR_ACCENT);
}

static void build_popup(const tour_view_t *view, panel_t *p)
{
    panel_start(p, POPUP_X + POPUP_BORDER, POPUP_Y + POPUP_BORDER,
                POPUP_W - 2 * POPUP_BORDER, POPUP_H - 2 * POPUP_BORDER, COLOR_PANEL);
    if (view->popup == TOUR_POPUP_GUIDE) {
        char title[64];
        snprintf(title, sizeof(title), "DIRECTIONS TO STOP %d", view->popup_stop + 1);
        panel_text(p, POPUP_TEXT_X, 48, 1, COLOR_DIM, title);
        const char *name = track_list_name(view->popup_stop);
        char name_line[64];
        snprintf(name_line, sizeof(name_line), "%.*s", (POPUP_TEXT_RIGHT - POPUP_TEXT_X) / CANVAS_CHAR_W(2),
                 name ? name : "");
        panel_text(p, POPUP_TEXT_X, 88, 2, COLOR_TEXT, name_line);

        char directions[GUIDE_TEXT_MAX];
        if (!guide_text_get(view->popup_stop, directions, sizeof(directions))) {
            directions[0] = '\0';
        }
        // Large text if it fits; otherwise the normal-size font.
        int body_height = POPUP_BODY_BOTTOM - POPUP_BODY_Y;
        int width1 = (POPUP_TEXT_RIGHT - POPUP_TEXT_X) / CANVAS_CHAR_W(1);
        int line_height1 = CANVAS_CHAR_H(1) + 4;
        int max_lines1 = body_height / line_height1;
        char lines[MAX_LINES][LINE_SIZE];
        int truncated = 0;
        gui_wrap_text(directions, width1, &lines[0][0], LINE_SIZE, max_lines1, &truncated);
        if (!truncated) {
            panel_wrapped(p, POPUP_TEXT_X, POPUP_BODY_Y, 1, width1, max_lines1, line_height1,
                          COLOR_TEXT, directions, NULL);
        } else {
            int width0 = (POPUP_TEXT_RIGHT - POPUP_TEXT_X) / CANVAS_CHAR_W(0);
            int line_height0 = CANVAS_CHAR_H(0) + 4;
            panel_wrapped(p, POPUP_TEXT_X, POPUP_BODY_Y, 0, width0, body_height / line_height0,
                          line_height0, COLOR_TEXT, directions, NULL);
        }
    } else {
        panel_text_centered(p, 400, 110, 3, COLOR_TEXT, "TOUR COMPLETE");
        panel_text_centered(p, 400, 220, 1, COLOR_TEXT, "Thanks for taking the");
        panel_text_centered(p, 400, 260, 1, COLOR_TEXT, "SFU Burnaby campus tour!");
        panel_text_centered(p, 400, 330, 0, COLOR_DIM, "Turn the knob to choose any stop again.");
    }
    panel_rule(p, POPUP_X + 40, POPUP_FOOTER_Y - 12, POPUP_W - 80, 2, COLOR_DIVIDER);
    panel_text_centered(p, 400, POPUP_FOOTER_Y, 0, COLOR_AMBER, "Press down on the joystick to continue");
}

#define POPUP_ITEMS_PER_PIECE 2

static int popup_piece_count(const panel_t *p)
{
    return 1 + (p->count + POPUP_ITEMS_PER_PIECE - 1) / POPUP_ITEMS_PER_PIECE;
}

// piece 0 = frame; piece k = the next 2 items; the last piece also fills the space below.
static void paint_popup_piece(const panel_t *p, int piece)
{
    if (piece == 0) {
        paint_popup_frame();
        return;
    }
    int first = (piece - 1) * POPUP_ITEMS_PER_PIECE;
    int last = first + POPUP_ITEMS_PER_PIECE;
    if (last >= p->count) {
        last = p->count;
    }
    panel_paint(p, first, last, last == p->count);
}

static void paint_popup(const tour_view_t *view)
{
    panel_t p;
    build_popup(view, &p);
    int pieces = popup_piece_count(&p);
    for (int piece = 0; piece < pieces; piece++) {
        paint_popup_piece(&p, piece);
    }
}

// ---------------------------------------------------------------------------
void gui_render(const tour_view_t *view)
{
    if (canvas == NULL) {
        return;
    }
    if (view->popup != TOUR_POPUP_NONE) {
        if (!drawn.valid || drawn.popup != view->popup || drawn.popup_stop != view->popup_stop) {
            paint_popup(view);
            drawn.popup = view->popup;
            drawn.popup_stop = view->popup_stop;
        }
        drawn.valid = true;          // the popup is up to date ...
        return;
    }
    if (drawn.popup != TOUR_POPUP_NONE) {
        drawn.valid = false;         // ... but everything under it must be redrawn when it closes
        drawn.popup = TOUR_POPUP_NONE;
    }

    if (!drawn.valid) {
        // Paint every piece over whatever is there (no clearing first: no flash).
        drawn.first_visible = choose_first_visible(view, -1);
        for (int piece = 0; piece < MAIN_PIECES; piece++) {
            paint_main_piece(view, piece);
        }
        drawn.valid = true;
        return;
    }
    if (drawn.left_playing != view->playing) {
        paint_left_panel(view, 0);
    }
    update_list(view);
    update_bar(view);
    update_time(view);
    if (drawn.icon_mode != view->mode) {
        paint_indicator(view);
    }
    update_volume(view);
}

void gui_refresh_step(const tour_view_t *view)
{
    if (canvas == NULL || !drawn.valid) {
        return;
    }
    if (view->popup != refresh_popup || view->popup_stop != refresh_popup_stop) {
        refresh_popup = view->popup;           // a different screen: start its cycle from the top
        refresh_popup_stop = view->popup_stop;
        refresh_piece = 0;
    }
    if (view->popup != TOUR_POPUP_NONE) {
        panel_t p;
        build_popup(view, &p);
        int pieces = popup_piece_count(&p);
        paint_popup_piece(&p, refresh_piece % pieces);
        refresh_piece = (refresh_piece + 1) % pieces;
    } else {
        paint_main_piece(view, refresh_piece % MAIN_PIECES);
        refresh_piece = (refresh_piece + 1) % MAIN_PIECES;
    }
}

int gui_refresh_piece_count(const tour_view_t *view)
{
    if (view->popup != TOUR_POPUP_NONE) {
        panel_t p;
        build_popup(view, &p);
        return popup_piece_count(&p);
    }
    return MAIN_PIECES;
}
