#include "device.h"
#include "../string.h"

static int null_read(struct device *dev, uint8_t *buf, uint32_t count) {
    (void)dev; (void)buf; (void)count;
    return 0;  /* EOF */
}

static int null_write(struct device *dev, const uint8_t *buf, uint32_t count) {
    (void)dev; (void)buf;
    return (int)count;  /* Discard */
}

void dev_null_init(void) {
    device_register("null", null_read, null_write);
}
