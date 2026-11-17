#ifndef ENCODER_H_
#define ENCODER_H_

// Using LIBGPIOD V2 API (more visible in .c)
typedef void(*Encoder_callback_t)(int direction);
void Encoder_init(Encoder_callback_t onTurn);
void Encoder_cleanup(void);

#endif
