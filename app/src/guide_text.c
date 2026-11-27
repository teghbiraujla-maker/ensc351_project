#include "guide_text.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "track_list.h"

// Characters Word / Google Docs like to insert, and what to show instead.
static const char *replacement_for(unsigned long codepoint)
{
    switch (codepoint) {
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: return "'";
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: return "\"";
    case 0x2010: case 0x2011: case 0x2012: case 0x2013:
    case 0x2014: case 0x2015: case 0x2212:              return "-";
    case 0x2026:                                        return "...";
    case 0x2022: case 0x00B7:                           return "*";
    case 0x2192:                                        return "->";
    case 0x2190:                                        return "<-";
    default:                                            return NULL;
    }
}

void guide_text_to_latin1(const char *in, char *out, size_t out_size)
{
    size_t length = 0;
    const unsigned char *p = (const unsigned char *)in;

#define PUT(c) do { if (length + 1 < out_size) out[length++] = (char)(c); } while (0)

    while (*p != '\0') {
        unsigned long codepoint;
        int extra;
        if (*p < 0x80)                { codepoint = *p;        extra = 0; }
        else if ((*p & 0xE0) == 0xC0) { codepoint = *p & 0x1F; extra = 1; }
        else if ((*p & 0xF0) == 0xE0) { codepoint = *p & 0x0F; extra = 2; }
        else if ((*p & 0xF8) == 0xF0) { codepoint = *p & 0x07; extra = 3; }
        else                          { codepoint = *p;        extra = -1; }   // not UTF-8: assume Latin-1

        bool valid = (extra >= 0);
        for (int i = 1; valid && i <= extra; i++) {
            if ((p[i] & 0xC0) != 0x80) {
                valid = false;
            } else {
                codepoint = (codepoint << 6) | (p[i] & 0x3F);
            }
        }
        if (!valid) {                     // stray byte: keep it as a Latin-1 character
            codepoint = *p;
            extra = 0;
        }
        p += 1 + extra;

        const char *replacement = replacement_for(codepoint);
        if (replacement != NULL) {
            for (const char *r = replacement; *r; r++) PUT(*r);
        } else if (codepoint == '\r' || codepoint == 0xFEFF) {
            // drop (Windows line ends, byte-order mark)
        } else if (codepoint == '\t' || codepoint == 0x00A0) {
            PUT(' ');
        } else if (codepoint == '\n') {
            PUT('\n');
        } else if (codepoint < 0x20 || (codepoint >= 0x7F && codepoint < 0xA0)) {
            // other control characters: drop
        } else if (codepoint < 0x100) {
            PUT(codepoint);               // ASCII and Latin-1 (accented letters)
        } else {
            PUT('?');                     // not in the display's font
        }
    }
#undef PUT
    out[length] = '\0';

    // Trim whitespace at both ends.
    while (length > 0 && isspace((unsigned char)out[length - 1])) {
        out[--length] = '\0';
    }
    size_t start = 0;
    while (out[start] != '\0' && isspace((unsigned char)out[start])) {
        start++;
    }
    if (start > 0) {
        memmove(out, out + start, length - start + 1);
    }
}

bool guide_text_get(int index, char *out, size_t out_size)
{
    if (index <= 0 || index >= track_list_count()) {
        return false;                     // stop 1 (or no such stop): no directions
    }
    snprintf(out, out_size, "temp directions to location %d", index + 1);

    // .../03_Convo_and_DFA.mp3 -> .../03_Convo_and_DFA.txt
    char path[1024];
    snprintf(path, sizeof(path), "%s", track_list_path(index));
    char *dot = strrchr(path, '.');
    if (dot == NULL || strcasecmp(dot, ".mp3") != 0) {
        return true;
    }
    snprintf(dot, sizeof(path) - (size_t)(dot - path), ".txt");

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return true;                      // no file yet: placeholder
    }
    char raw[GUIDE_TEXT_MAX * 2];
    size_t got = fread(raw, 1, sizeof(raw) - 1, file);
    fclose(file);
    raw[got] = '\0';

    char converted[GUIDE_TEXT_MAX];
    guide_text_to_latin1(raw, converted, sizeof(converted));
    if (converted[0] != '\0') {
        snprintf(out, out_size, "%s", converted);
    }
    return true;
}
