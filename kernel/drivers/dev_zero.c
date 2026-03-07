#include "device.h"
#include "../string.h"

static int zero_read(struct device *dev, uint8_t *buf, uint32_t count) {
    (void)dev;
    memset(buf, 0, count);
    return (int)count;
}

static int zero_write(struct device *dev, const uint8_t *buf, uint32_t count) {
    (void)dev; (void)buf;
    return (int)count;  /* Discard */
}

void dev_zero_init(void) {
    device_register("zero", zero_read, zero_write);
}
