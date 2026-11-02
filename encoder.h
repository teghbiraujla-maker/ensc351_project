#ifndef _ENCODER_H_
#define _ENCODER_H_

#include <stdbool.h>

void encoder_init(void);
void encoder_cleanup(void);

long long encoder_get_count(void);

#endif
