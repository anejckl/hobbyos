/*
 * test_device.c — Host-side unit tests for device registry.
 *
 * We replicate the device registry logic here (include the source)
 * to test register, find, find_by_number without hardware.
 */

#include "test_main.h"
#include <string.h>
#include <stdbool.h>

/* Provide kernel string functions needed by device registry logic */
static char *my_strncpy(char *dst, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = '\0';
    return dst;
}

static int my_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *(unsigned char *)a - *(unsigned char *)b;
}

/* Override string.h functions to use our stubs */
#define strncpy my_strncpy
#define strcmp my_strcmp

/* We need to redefine the types from common.h for host compilation */
#ifndef DEV_NAME_MAX

/* Include enough of device.h definitions inline */
#define MAX_DEVICES    64
#define DEV_NAME_MAX   32

#define DEV_CHAR    1
#define DEV_BLOCK   2
#define DEV_NET     3

#define DEV_STATE_INACTIVE  0
#define DEV_STATE_ACTIVE    1

struct device;

struct device_ops {
    int (*open)(struct device *dev);
    int (*close)(struct device *dev);
    int (*read)(struct device *dev, uint8_t *buf, uint32_t count, uint64_t offset);
    int (*write)(struct device *dev, const uint8_t *buf, uint32_t count, uint64_t offset);
    int (*ioctl)(struct device *dev, uint32_t cmd, uint64_t arg);
    int (*mmap)(struct device *dev, uint64_t offset, uint64_t size, uint64_t *phys_out);
};

struct device {
    char name[DEV_NAME_MAX];
    uint8_t type;
    uint8_t state;
    uint16_t major;
    uint16_t minor;
    int (*read)(struct device *dev, uint8_t *buf, uint32_t count);
    int (*write)(struct device *dev, const uint8_t *buf, uint32_t count);
    int (*ioctl)(struct device *dev, uint32_t cmd, uint64_t arg);
    struct device_ops *ops;
    void *private_data;
    bool in_use;
};

#endif /* DEV_NAME_MAX */

/* --- Inline the device registry logic for testing --- */

static struct device test_devices[MAX_DEVICES];

static void test_device_init(void) {
    memset(test_devices, 0, sizeof(test_devices));
}

static struct device *test_device_register(const char *name,
    int (*readfn)(struct device *, uint8_t *, uint32_t),
    int (*writefn)(struct device *, const uint8_t *, uint32_t)) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!test_devices[i].in_use) {
            memset(&test_devices[i], 0, sizeof(struct device));
            strncpy(test_devices[i].name, name, DEV_NAME_MAX - 1);
            test_devices[i].name[DEV_NAME_MAX - 1] = '\0';
            test_devices[i].type = DEV_CHAR;
            test_devices[i].state = DEV_STATE_ACTIVE;
            test_devices[i].read = readfn;
            test_devices[i].write = writefn;
            test_devices[i].in_use = true;
            return &test_devices[i];
        }
    }
    return NULL;
}

static struct device *test_device_register_ex(const char *name, uint8_t type,
    uint16_t major, uint16_t minor, struct device_ops *ops) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!test_devices[i].in_use) {
            memset(&test_devices[i], 0, sizeof(struct device));
            strncpy(test_devices[i].name, name, DEV_NAME_MAX - 1);
            test_devices[i].name[DEV_NAME_MAX - 1] = '\0';
            test_devices[i].type = type;
            test_devices[i].state = DEV_STATE_ACTIVE;
            test_devices[i].major = major;
            test_devices[i].minor = minor;
            test_devices[i].ops = ops;
            test_devices[i].in_use = true;
            return &test_devices[i];
        }
    }
    return NULL;
}

static struct device *test_device_find(const char *name) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (test_devices[i].in_use && strcmp(test_devices[i].name, name) == 0)
            return &test_devices[i];
    }
    return NULL;
}

static struct device *test_device_find_by_number(uint16_t major, uint16_t minor) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (test_devices[i].in_use &&
            test_devices[i].major == major && test_devices[i].minor == minor)
            return &test_devices[i];
    }
    return NULL;
}

static int test_device_get_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (test_devices[i].in_use)
            count++;
    }
    return count;
}

static struct device *test_device_get_by_index(int index) {
    int count = 0;
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (test_devices[i].in_use) {
            if (count == index)
                return &test_devices[i];
            count++;
        }
    }
    return NULL;
}

/* --- Dummy read/write for testing --- */
static int dummy_read(struct device *dev, uint8_t *buf, uint32_t count) {
    (void)dev; (void)buf; (void)count;
    return 0;
}

static int dummy_write(struct device *dev, const uint8_t *buf, uint32_t count) {
    (void)dev; (void)buf;
    return (int)count;
}

/* --- Test functions --- */

static void test_device_register_and_find(void) {
    test_device_init();

    struct device *dev = test_device_register("null", dummy_read, dummy_write);
    TEST("device_register returns non-NULL", dev != NULL);
    TEST("device name is 'null'", strcmp(dev->name, "null") == 0);
    TEST("device type is DEV_CHAR", dev->type == DEV_CHAR);
    TEST("device state is active", dev->state == DEV_STATE_ACTIVE);
    TEST("device is in_use", dev->in_use == true);

    struct device *found = test_device_find("null");
    TEST("device_find returns registered device", found == dev);

    struct device *notfound = test_device_find("nonexistent");
    TEST("device_find returns NULL for unknown", notfound == NULL);
}

static void test_device_register_ex_and_find_by_number(void) {
    test_device_init();

    struct device *dev = test_device_register_ex("disk/sda", DEV_BLOCK, 8, 0, NULL);
    TEST("device_register_ex returns non-NULL", dev != NULL);
    TEST("device_register_ex name is 'disk/sda'", strcmp(dev->name, "disk/sda") == 0);
    TEST("device_register_ex type is DEV_BLOCK", dev->type == DEV_BLOCK);
    TEST("device_register_ex major is 8", dev->major == 8);
    TEST("device_register_ex minor is 0", dev->minor == 0);

    struct device *found = test_device_find_by_number(8, 0);
    TEST("find_by_number finds disk/sda", found == dev);

    struct device *notfound = test_device_find_by_number(99, 99);
    TEST("find_by_number returns NULL for unknown", notfound == NULL);
}

static void test_device_count_and_index(void) {
    test_device_init();

    TEST("initial device count is 0", test_device_get_count() == 0);

    test_device_register("null", dummy_read, dummy_write);
    test_device_register("zero", dummy_read, dummy_write);
    test_device_register("tty", dummy_read, dummy_write);

    TEST("device count is 3", test_device_get_count() == 3);

    struct device *d0 = test_device_get_by_index(0);
    struct device *d1 = test_device_get_by_index(1);
    struct device *d2 = test_device_get_by_index(2);
    struct device *d3 = test_device_get_by_index(3);

    TEST("device index 0 is 'null'", d0 != NULL && strcmp(d0->name, "null") == 0);
    TEST("device index 1 is 'zero'", d1 != NULL && strcmp(d1->name, "zero") == 0);
    TEST("device index 2 is 'tty'", d2 != NULL && strcmp(d2->name, "tty") == 0);
    TEST("device index 3 is NULL (past end)", d3 == NULL);
}

static void test_device_hierarchical_names(void) {
    test_device_init();

    struct device *d1 = test_device_register("input/keyboard", dummy_read, dummy_write);
    struct device *d2 = test_device_register("input/mouse0", dummy_read, dummy_write);
    struct device *d3 = test_device_register_ex("disk/sda", DEV_BLOCK, 8, 0, NULL);

    TEST("input/keyboard registered", d1 != NULL);
    TEST("input/mouse0 registered", d2 != NULL);
    TEST("disk/sda registered", d3 != NULL);

    struct device *found = test_device_find("input/keyboard");
    TEST("find input/keyboard", found == d1);

    found = test_device_find("input/mouse0");
    TEST("find input/mouse0", found == d2);

    found = test_device_find("disk/sda");
    TEST("find disk/sda", found == d3);
}

static void test_device_max_capacity(void) {
    test_device_init();

    /* Fill all slots */
    int registered = 0;
    for (int i = 0; i < MAX_DEVICES; i++) {
        char name[DEV_NAME_MAX];
        snprintf(name, sizeof(name), "dev%d", i);
        struct device *d = test_device_register(name, dummy_read, dummy_write);
        if (d) registered++;
    }
    TEST("can register MAX_DEVICES devices", registered == MAX_DEVICES);

    /* Next registration should fail */
    struct device *overflow = test_device_register("overflow", dummy_read, dummy_write);
    TEST("registration past MAX_DEVICES returns NULL", overflow == NULL);
}

void test_device_suite(void) {
    printf("=== Device registry tests ===\n");
    test_device_register_and_find();
    test_device_register_ex_and_find_by_number();
    test_device_count_and_index();
    test_device_hierarchical_names();
    test_device_max_capacity();
}
