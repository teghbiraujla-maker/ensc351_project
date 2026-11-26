// Hardware-free test for playback_display (needs no audio device).
#include <stdio.h>
#include <string.h>
#include "playback_display.h"

static int failures = 0;
#define CHECK_STR(actual, expected) do { \
    if (strcmp((actual), (expected)) != 0) { \
        printf("FAIL line %d: got '%s', expected '%s'\n", __LINE__, (actual), (expected)); \
        failures++; } } while (0)
#define CHECK_NUM(actual, expected) do { \
    double a_ = (actual), e_ = (expected); \
    if (a_ < e_ - 1e-9 || a_ > e_ + 1e-9) { \
        printf("FAIL line %d: got %f, expected %f\n", __LINE__, a_, e_); \
        failures++; } } while (0)

int main(void)
{
    char buf[64];

    playback_display_time(0, 204, buf, sizeof(buf));      CHECK_STR(buf, "0:00/3:24");
    playback_display_time(154, 204, buf, sizeof(buf));    CHECK_STR(buf, "2:34/3:24");
    playback_display_time(59.9, 204, buf, sizeof(buf));   CHECK_STR(buf, "0:59/3:24");
    playback_display_time(60, 204, buf, sizeof(buf));     CHECK_STR(buf, "1:00/3:24");
    playback_display_time(120, 204, buf, sizeof(buf));    CHECK_STR(buf, "2:00/3:24");
    playback_display_time(300, 204, buf, sizeof(buf));    CHECK_STR(buf, "3:24/3:24"); // clamped
    playback_display_time(-5, 0, buf, sizeof(buf));       CHECK_STR(buf, "0:00/0:00");

    audio_status_t st = { AUDIO_PLAYING, 154.0, 204.0, 50 };
    playback_display_line(&st, 3, "Convocation Mall", buf, sizeof(buf));
    CHECK_STR(buf, "3 Convocation Mall  2:34/3:24 PLAYING");
    st.state = AUDIO_PAUSED;
    playback_display_line(&st, 3, "Convocation Mall", buf, sizeof(buf));
    CHECK_STR(buf, "3 Convocation Mall  2:34/3:24 PAUSED");
    st.state = AUDIO_STOPPED;
    playback_display_line(&st, 3, "Convocation Mall", buf, sizeof(buf));
    CHECK_STR(buf, "STOPPED");

    // Single clock values, and h:mm:ss from one hour up
    playback_display_clock(0, buf, sizeof(buf));          CHECK_STR(buf, "0:00");
    playback_display_clock(443, buf, sizeof(buf));        CHECK_STR(buf, "7:23");
    playback_display_clock(3599.9, buf, sizeof(buf));     CHECK_STR(buf, "59:59");
    playback_display_clock(3600, buf, sizeof(buf));       CHECK_STR(buf, "1:00:00");
    playback_display_clock(-3, buf, sizeof(buf));         CHECK_STR(buf, "0:00");
    playback_display_time(204, 443, buf, sizeof(buf));    CHECK_STR(buf, "3:24/7:23");   // the README example
    playback_display_time(65, 3725, buf, sizeof(buf));    CHECK_STR(buf, "0:01:05/1:02:05");

    // Progress bar fraction: position / duration, clamped to 0..1
    audio_status_t p = { AUDIO_PLAYING, 0.0, 0.0, 50 };
    CHECK_NUM(playback_display_progress(&p), 0.0);          // length unknown
    p.duration_sec = 200.0; p.position_sec = 100.0;
    CHECK_NUM(playback_display_progress(&p), 0.5);          // half way = half full
    p.position_sec = 50.0;
    CHECK_NUM(playback_display_progress(&p), 0.25);
    p.position_sec = 250.0;
    CHECK_NUM(playback_display_progress(&p), 1.0);          // never more than full
    p.position_sec = -1.0;
    CHECK_NUM(playback_display_progress(&p), 0.0);

    if (failures == 0) printf("playback_display: all tests passed\n");
    return failures ? 1 : 0;
}
