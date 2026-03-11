#ifndef MOUSE_H
#define MOUSE_H

#include "../common.h"

struct mouse_event {
    int16_t dx;
    int16_t dy;
    uint8_t buttons;  /* bit0=left, bit1=right, bit2=middle */
};

void mouse_init(void);
int  mouse_read_event(struct mouse_event *ev);  /* 0 if empty, 1 if got event */

#endif /* MOUSE_H */
