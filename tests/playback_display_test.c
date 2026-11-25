// Hardware-free test for playback_display (needs no audio device).
#include <stdio.h>
#include <string.h>
#include "playback_display.h"

static int failures = 0;
#define CHECK_STR(actual, expected) do { \
    if (strcmp((actual), (expected)) != 0) { \
        printf("FAIL line %d: got '%s', expected '%s'\n", __LINE__, (actual), (expected)); \
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

    if (failures == 0) printf("playback_display: all tests passed\n");
    return failures ? 1 : 0;
}
