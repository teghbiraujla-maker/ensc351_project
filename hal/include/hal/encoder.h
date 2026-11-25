#ifndef ENCODER_H_
#define ENCODER_H_

#include <stdbool.h>

// Using LIBGPIOD V2 API (more visible in .c)
typedef void(*Encoder_callback_t)(int direction);
typedef void(*Encoder_button_callback_t)(void);
bool Encoder_init(Encoder_callback_t onTurn, Encoder_button_callback_t onPress);
void Encoder_cleanup(void);

#endif
