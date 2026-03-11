#ifndef LIBM_H
#define LIBM_H

typedef unsigned long long uint64_t;
typedef signed long long int64_t;
typedef unsigned int uint32_t;
typedef signed int int32_t;

/* Integer math functions (no float, no SSE) */
uint64_t isqrt(uint64_t n);
int64_t  ipow(int64_t base, uint32_t exp);
int64_t  iabs(int64_t x);

/* Fixed-point trig: input in millidegrees (0-359999), output scaled by 1024 */
int32_t  isin(int32_t deg_milli);
int32_t  icos(int32_t deg_milli);

#endif /* LIBM_H */
