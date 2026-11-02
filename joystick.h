#ifndef _JOYSTICK_H_
#define _JOYSTICK_H_

#include <stdbool.h>


void joystick_init(void);
void joystick_cleanup(void);

int joystick_read_raw_x(void);
int joystick_read_raw_y(void);

#endif
