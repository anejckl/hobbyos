#include "test_main.h"
#include <string.h>

/* Replicate the ip_checksum function for testing */
static uint16_t test_ip_checksum(const void *data, uint32_t len) {
    const uint16_t *ptr = (const uint16_t *)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len == 1)
        sum += *(const uint8_t *)ptr;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)(~sum);
}

void test_checksum_basic(void) {
    /* Test with known IPv4 header */
    uint8_t ip_header[] = {
        0x45, 0x00, 0x00, 0x3c, 0x1c, 0x46, 0x40, 0x00,
        0x40, 0x06, 0x00, 0x00, /* checksum field = 0 */
        0xac, 0x10, 0x0a, 0x63, /* src: 172.16.10.99 */
        0xac, 0x10, 0x0a, 0x0c  /* dst: 172.16.10.12 */
    };
    uint16_t cksum = test_ip_checksum(ip_header, 20);
    TEST("ip_checksum non-zero for zeroed field", cksum != 0);

    /* Verify: set checksum field and recheck — should be 0 */
    ip_header[10] = (uint8_t)(cksum & 0xFF);
    ip_header[11] = (uint8_t)(cksum >> 8);
    uint16_t verify = test_ip_checksum(ip_header, 20);
    TEST("ip_checksum verifies to 0", verify == 0);
}

void test_checksum_zeros(void) {
    uint8_t zeros[20];
    memset(zeros, 0, sizeof(zeros));
    uint16_t cksum = test_ip_checksum(zeros, 20);
    TEST("ip_checksum of all zeros is 0xFFFF", cksum == 0xFFFF);
}

void test_checksum_odd_length(void) {
    uint8_t data[] = {0x01, 0x02, 0x03};
    uint16_t cksum = test_ip_checksum(data, 3);
    TEST("ip_checksum handles odd length", cksum != 0);
}

void test_checksum_suite(void) {
    printf("=== IP checksum tests ===\n");
    test_checksum_basic();
    test_checksum_zeros();
    test_checksum_odd_length();
}
