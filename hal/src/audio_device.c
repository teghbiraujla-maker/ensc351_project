#include "hal/audio_device.h"
#include <stdio.h>
#include <string.h>

#define CARDS_FILE "/proc/asound/cards"

// /proc/asound/cards looks like:
//  0 [vc4hdmi0       ]: vc4-hdmi - vc4-hdmi-0
//                       vc4-hdmi-0
//  1 [Device         ]: USB-Audio - USB Audio Device
//                       C-Media Electronics Inc. USB Audio Device at usb-...
bool audio_device_find_usb(const char *cards_file, char *out, size_t out_size)
{
    FILE *file = fopen(cards_file, "r");
    if (file == NULL) {
        return false;
    }

    char line[256];
    bool found = false;
    while (!found && fgets(line, sizeof(line), file) != NULL) {
        int card_number = -1;
        // Only the header line of each card starts with its number.
        if (sscanf(line, " %d [", &card_number) == 1 && strstr(line, "USB-Audio") != NULL) {
            // plughw: lets ALSA convert sample rate/format to what the adapter supports.
            snprintf(out, out_size, "plughw:%d,0", card_number);
            found = true;
        }
    }
    fclose(file);
    return found;
}

bool audio_device_resolve(const char *requested, char *out, size_t out_size)
{
    if (requested != NULL && strcmp(requested, "auto") != 0 && requested[0] != '\0') {
        snprintf(out, out_size, "%s", requested);
        return true;
    }
    if (audio_device_find_usb(CARDS_FILE, out, out_size)) {
        return true;
    }
    snprintf(out, out_size, "default");
    return false;
}
