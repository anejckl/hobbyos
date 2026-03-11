#include "libm.h"

uint64_t isqrt(uint64_t n) {
    if (n == 0) return 0;
    uint64_t x = n, y = (n + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}

int64_t ipow(int64_t base, uint32_t exp) {
    int64_t result = 1;
    while (exp > 0) {
        if (exp & 1) result *= base;
        base *= base;
        exp >>= 1;
    }
    return result;
}

int64_t iabs(int64_t x) { return x < 0 ? -x : x; }

/* sin table: sin_tbl[i] = round(sin(i degrees) * 1024), i=0..90 */
static const int16_t sin_tbl[91] = {
      0,   18,   36,   54,   71,   89,  107,  125,  143,  160,
    178,  195,  212,  230,  247,  264,  280,  297,  314,  330,
    346,  362,  378,  394,  409,  424,  439,  454,  469,  483,
    498,  512,  526,  539,  553,  566,  579,  591,  604,  616,
    627,  639,  650,  661,  671,  681,  691,  701,  710,  719,
    728,  736,  744,  752,  759,  766,  773,  779,  785,  790,
    796,  801,  806,  810,  814,  818,  822,  825,  828,  831,
    833,  835,  837,  839,  840,  841,  842,  843,  843,  843,
    843,  843,  843,  843,  843,  843,  843,  843,  843,  843,
    843
};

static int32_t isin_deg(int32_t deg) {
    deg = deg % 360;
    if (deg < 0) deg += 360;
    if (deg <= 90)  return (int32_t)sin_tbl[deg];
    if (deg <= 180) return (int32_t)sin_tbl[180 - deg];
    if (deg <= 270) return -(int32_t)sin_tbl[deg - 180];
    return -(int32_t)sin_tbl[360 - deg];
}

int32_t isin(int32_t deg_milli) {
    int32_t d = deg_milli / 1000;
    return isin_deg(d);
}

int32_t icos(int32_t deg_milli) {
    return isin(deg_milli + 90000);
}
