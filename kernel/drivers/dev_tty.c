#include "device.h"
#include "tty.h"

static int dev_tty_read(struct device *dev, uint8_t *buf, uint32_t count) {
    (void)dev;
    return tty_read(buf, count);
}

static int dev_tty_write(struct device *dev, const uint8_t *buf, uint32_t count) {
    (void)dev;
    return tty_write(buf, count);
}

void dev_tty_init(void) {
    device_register("tty", dev_tty_read, dev_tty_write);
}
