// Host-side test of the directions text files.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "guide_text.h"
#include "track_list.h"

static int failures = 0;

#define CHECK(condition, description) do { \
    int ok_ = (condition); \
    printf("  [%s] %s\n", ok_ ? "PASS" : "FAIL", description); \
    if (!ok_) { printf("         (line %d: %s)\n", __LINE__, #condition); failures++; } } while (0)

static char folder[] = "/tmp/guide_text_test_XXXXXX";

static void write_file(const char *name, const char *contents)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", folder, name);
    FILE *f = fopen(path, "w");
    if (f) { fputs(contents, f); fclose(f); }
}

static void remove_file(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", folder, name);
    unlink(path);
}

int main(void)
{
    char out[GUIDE_TEXT_MAX];

    printf("Text conversion for the screen's font\n");
    guide_text_to_latin1("plain text", out, sizeof(out));
    CHECK(!strcmp(out, "plain text"), "ASCII unchanged");
    guide_text_to_latin1("\xE2\x80\x9CQuad\xE2\x80\x9D \xE2\x80\x94 it\xE2\x80\x99s here\xE2\x80\xA6", out, sizeof(out));
    CHECK(!strcmp(out, "\"Quad\" - it's here..."), "Word-style quotes, dashes, ellipsis -> ASCII");
    guide_text_to_latin1("caf\xC3\xA9", out, sizeof(out));
    CHECK(!strcmp(out, "caf\xE9"), "accented letters kept (UTF-8 -> Latin-1)");
    guide_text_to_latin1("  line one\r\nline\ttwo \n\n", out, sizeof(out));
    CHECK(!strcmp(out, "line one\nline two"), "Windows line ends, tabs and surrounding space cleaned");
    guide_text_to_latin1("\xEF\xBB\xBFstart", out, sizeof(out));
    CHECK(!strcmp(out, "start"), "byte-order mark dropped");
    guide_text_to_latin1("go \xE2\x86\x92 left \xE4\xB8\xAD", out, sizeof(out));
    CHECK(!strcmp(out, "go -> left ?"), "arrow -> '->', characters outside the font -> '?'");

    printf("Files next to the MP3s\n");
    if (mkdtemp(folder) == NULL) return 2;
    write_file("01_Start.mp3", "");
    write_file("02_Library.mp3", "");
    write_file("03_Quad.mp3", "");
    write_file("02_Library.txt", "Walk north to the library.\n");
    track_list_load(folder);
    CHECK(!guide_text_get(0, out, sizeof(out)), "stop 1: no directions");
    CHECK(guide_text_get(1, out, sizeof(out)) && !strcmp(out, "Walk north to the library."), "stop 2: text from 02_Library.txt");
    CHECK(guide_text_get(2, out, sizeof(out)) && !strcmp(out, "temp directions to location 3"), "stop 3: no file -> placeholder");
    write_file("03_Quad.txt", "   \n");
    CHECK(guide_text_get(2, out, sizeof(out)) && !strcmp(out, "temp directions to location 3"), "empty file -> placeholder");
    CHECK(!guide_text_get(7, out, sizeof(out)), "no such stop: false");

    track_list_cleanup();
    remove_file("01_Start.mp3"); remove_file("02_Library.mp3"); remove_file("03_Quad.mp3");
    remove_file("02_Library.txt"); remove_file("03_Quad.txt");
    rmdir(folder);
    printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED",
           failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
