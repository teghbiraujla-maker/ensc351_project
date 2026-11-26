// Used instead of encoder.c when the project is built with -DENABLE_ENCODER=OFF
// (no libgpiod available). The program then runs with terminal input only.
#include "hal/encoder.h"
#include <stdio.h>

bool Encoder_init(Encoder_callback_t onTurn, Encoder_button_callback_t onPress)
{
    (void)onTurn;
    (void)onPress;
    fprintf(stderr, "encoder: not built into this program (ENABLE_ENCODER=OFF)\n");
    return false;
}

void Encoder_cleanup(void)
{
}
