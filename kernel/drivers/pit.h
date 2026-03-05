#ifndef PIT_H
#define PIT_H

#include "../common.h"

/* PIT ports */
#define PIT_CHANNEL0    0x40
#define PIT_CMD         0x43

/* PIT base frequency */
#define PIT_BASE_FREQ   1193182

void pit_init(uint32_t freq);
uint64_t pit_get_ticks(void);
uint64_t pit_get_uptime_seconds(void);

#endif /* PIT_H */
