#include "device.h"
#include "keyboard.h"
#include "mouse.h"
#include "../string.h"
#include "../debug/debug.h"

/* --- /dev/input/keyboard: raw scancode reads --- */

static int input_kb_read(struct device *dev, uint8_t *buf, uint32_t count) {
    (void)dev;
    if (count == 0) return 0;
    /* Non-blocking: return one character if available */
    if (keyboard_haschar()) {
        buf[0] = (uint8_t)keyboard_getchar();
        return 1;
    }
    return 0;  /* No data available */
}

static int input_kb_write(struct device *dev, const uint8_t *buf, uint32_t count) {
    (void)dev; (void)buf; (void)count;
    return -1;  /* Cannot write to keyboard */
}

/* --- /dev/input/mouse0: mouse event reads --- */

static int input_mouse_read(struct device *dev, uint8_t *buf, uint32_t count) {
    (void)dev;
    if (count < sizeof(struct mouse_event))
        return -1;
    struct mouse_event ev;
    if (mouse_read_event(&ev)) {
        memcpy(buf, &ev, sizeof(struct mouse_event));
        return (int)sizeof(struct mouse_event);
    }
    return 0;  /* No event */
}

static int input_mouse_write(struct device *dev, const uint8_t *buf, uint32_t count) {
    (void)dev; (void)buf; (void)count;
    return -1;  /* Cannot write to mouse */
}

void dev_input_init(void) {
    device_register("input/keyboard", input_kb_read, input_kb_write);
    device_register("input/mouse0", input_mouse_read, input_mouse_write);
    debug_printf("dev_input: /dev/input/keyboard and /dev/input/mouse0 registered\n");
}
