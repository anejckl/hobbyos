#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "../common.h"

/* Keyboard data port */
#define KEYBOARD_DATA_PORT 0x60

void keyboard_init(void);
char keyboard_getchar(void);     /* Blocking read */
int  keyboard_haschar(void);     /* Non-blocking check */

#endif /* KEYBOARD_H */
