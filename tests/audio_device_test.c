// Host-side test of the ALSA card discovery, using fake /proc/asound/cards files.
#include <stdio.h>
#include <string.h>
#include "hal/audio_device.h"

static int failures = 0;

static void check(bool ok, const char *description)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", description);
    if (!ok) {
        failures++;
    }
}

static void write_cards_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    fputs(text, file);
    fclose(file);
}

int main(void)
{
    const char *path = "/tmp/fake_asound_cards.txt";
    char device[64] = "unchanged";
    printf("Audio device discovery\n");

    write_cards_file(path,
        " 0 [vc4hdmi0       ]: vc4-hdmi - vc4-hdmi-0\n"
        "                      vc4-hdmi-0\n"
        " 2 [Device         ]: USB-Audio - USB Audio Device\n"
        "                      C-Media Electronics Inc. USB Audio Device at usb-xhci-hcd.1-1, full speed\n");
    check(audio_device_find_usb(path, device, sizeof(device)), "finds the USB card");
    check(strcmp(device, "plughw:2,0") == 0, "uses its card number (plughw:2,0)");

    write_cards_file(path,
        " 0 [vc4hdmi0       ]: vc4-hdmi - vc4-hdmi-0\n"
        "                      vc4-hdmi-0\n");
    strcpy(device, "unchanged");
    check(!audio_device_find_usb(path, device, sizeof(device)), "no USB card -> false");
    check(strcmp(device, "unchanged") == 0, "output untouched when nothing found");

    check(!audio_device_find_usb("/tmp/does_not_exist_cards", device, sizeof(device)), "missing file -> false");

    check(audio_device_resolve("plughw:5,0", device, sizeof(device)) && strcmp(device, "plughw:5,0") == 0,
          "an explicit device is used as given");

    printf("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
