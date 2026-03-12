#include "device.h"
#include "pit.h"
#include "../string.h"
#include "../debug/debug.h"

/* xorshift64 PRNG state */
static uint64_t random_state = 0;

static uint64_t xorshift64(void) {
    uint64_t x = random_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    random_state = x;
    return x;
}

static int random_read(struct device *dev, uint8_t *buf, uint32_t count) {
    (void)dev;
    for (uint32_t i = 0; i < count; i++) {
        if ((i % 8) == 0) {
            uint64_t r = xorshift64();
            /* Fill up to 8 bytes from this value */
            uint32_t remaining = count - i;
            uint32_t chunk = remaining < 8 ? remaining : 8;
            for (uint32_t j = 0; j < chunk; j++) {
                buf[i + j] = (uint8_t)(r >> (j * 8));
            }
            i += chunk - 1;  /* -1 because the for loop increments */
        }
    }
    return (int)count;
}

static int random_write(struct device *dev, const uint8_t *buf, uint32_t count) {
    (void)dev;
    /* Writing to /dev/random seeds the PRNG */
    for (uint32_t i = 0; i < count && i < 8; i++) {
        random_state ^= ((uint64_t)buf[i]) << (i * 8);
    }
    /* Ensure state is never zero */
    if (random_state == 0)
        random_state = 0xDEADBEEFCAFEBABEULL;
    return (int)count;
}

void dev_random_init(void) {
    /* Seed from PIT ticks */
    random_state = pit_get_ticks();
    if (random_state == 0)
        random_state = 0x12345678ABCDEF01ULL;

    device_register("random", random_read, random_write);
    debug_printf("dev_random: /dev/random initialized\n");
}
