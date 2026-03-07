#ifndef KLOG_H
#define KLOG_H

#include "../debug/debug.h"

/* Log levels */
#define KLOG_LEVEL_ERR   0
#define KLOG_LEVEL_WARN  1
#define KLOG_LEVEL_INFO  2
#define KLOG_LEVEL_DBG   3

/* Current log level (compile-time default) */
#ifndef KLOG_LEVEL
#define KLOG_LEVEL KLOG_LEVEL_INFO
#endif

#define KLOG_ERR(fmt, ...) do { \
    if (KLOG_LEVEL >= KLOG_LEVEL_ERR) \
        debug_printf("[ERR] " fmt "\n", ##__VA_ARGS__); \
} while (0)

#define KLOG_WARN(fmt, ...) do { \
    if (KLOG_LEVEL >= KLOG_LEVEL_WARN) \
        debug_printf("[WARN] " fmt "\n", ##__VA_ARGS__); \
} while (0)

#define KLOG_INFO(fmt, ...) do { \
    if (KLOG_LEVEL >= KLOG_LEVEL_INFO) \
        debug_printf("[INFO] " fmt "\n", ##__VA_ARGS__); \
} while (0)

#define KLOG_DBG(fmt, ...) do { \
    if (KLOG_LEVEL >= KLOG_LEVEL_DBG) \
        debug_printf("[DBG] " fmt "\n", ##__VA_ARGS__); \
} while (0)

#endif /* KLOG_H */
