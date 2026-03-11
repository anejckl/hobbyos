#include "mouse.h"
#include "device.h"
#include "../arch/x86_64/isr.h"
#include "../arch/x86_64/pic.h"
#include "../string.h"
#include "../debug/debug.h"

#define MOUSE_IRQ        12
#define PS2_DATA_PORT    0x60
#define PS2_CMD_PORT     0x64
#define PS2_STATUS_PORT  0x64

#define RING_SIZE 64

static struct mouse_event ring[RING_SIZE];
static volatile int ring_head = 0;
static volatile int ring_tail = 0;

/* PS/2 packet accumulation */
static uint8_t  pkt[3];
static int      pkt_byte = 0;

static void ring_push(struct mouse_event *ev) {
    int next = (ring_head + 1) % RING_SIZE;
    if (next != ring_tail) {
        ring[ring_head] = *ev;
        ring_head = next;
    }
}

static void ps2_wait_write(void) {
    int timeout = 1000;
    while (timeout-- > 0 && (inb(PS2_STATUS_PORT) & 2)) {
        /* wait for input buffer empty */
    }
}

static void ps2_wait_read(void) {
    int timeout = 1000;
    while (timeout-- > 0 && !(inb(PS2_STATUS_PORT) & 1)) {
        /* wait for output buffer full (data available) */
    }
}

static void ps2_send_cmd(uint8_t cmd) {
    ps2_wait_write();
    outb(PS2_CMD_PORT, cmd);
}

static void ps2_send_data(uint8_t data) {
    ps2_wait_write();
    outb(PS2_DATA_PORT, data);
}

static void ps2_flush(void) {
    /* Drain any pending bytes from the PS/2 output buffer */
    int timeout = 100;
    while (timeout-- > 0 && (inb(PS2_STATUS_PORT) & 1)) {
        inb(PS2_DATA_PORT);
    }
}

static void mouse_handler(struct interrupt_frame *frame) {
    (void)frame;

    /* Check PS/2 status: bit 5 (AUXDATA) must be set for mouse data.
     * If not set, this is a keyboard byte routed to IRQ 12 — skip it. */
    uint8_t status = inb(PS2_STATUS_PORT);
    if (!(status & 0x20)) {
        inb(PS2_DATA_PORT);  /* drain the keyboard byte */
        return;
    }

    uint8_t data = inb(PS2_DATA_PORT);

    pkt[pkt_byte++] = data;
    if (pkt_byte == 3) {
        pkt_byte = 0;
        /* Validate: bit 3 of first byte must be set */
        if (!(pkt[0] & 0x08)) return;

        struct mouse_event ev;
        ev.buttons = pkt[0] & 0x07;
        ev.dx = (int16_t)((pkt[0] & 0x10) ? pkt[1] - 256 : pkt[1]);
        ev.dy = (int16_t)((pkt[0] & 0x20) ? pkt[2] - 256 : pkt[2]);
        ev.dy = (int16_t)(-ev.dy); /* Y is inverted in PS/2 */

        ring_push(&ev);
    }
    /* EOI is sent by isr_handler() before dispatching — no manual EOI needed */
}

static int mouse_dev_read(struct device *dev, uint8_t *buf, uint32_t count) {
    (void)dev;
    if (count < sizeof(struct mouse_event)) return -1;
    if (ring_head == ring_tail) return 0; /* no event */
    struct mouse_event *ev = (struct mouse_event *)buf;
    *ev = ring[ring_tail];
    ring_tail = (ring_tail + 1) % RING_SIZE;
    return (int)sizeof(struct mouse_event);
}

static int mouse_dev_write(struct device *dev, const uint8_t *buf, uint32_t count) {
    (void)dev; (void)buf; (void)count;
    return -1;
}

void mouse_init(void) {
    /* Enable auxiliary PS/2 device (mouse) */
    ps2_send_cmd(0xA8);

    /* Drain any stale bytes from PS/2 output buffer */
    ps2_flush();

    /* Read controller configuration byte */
    ps2_send_cmd(0x20);
    ps2_wait_read();
    uint8_t config = inb(PS2_DATA_PORT);

    /* Enable IRQs for both keyboard (bit 0) and mouse (bit 1),
     * and ensure both port clocks are enabled (clear bits 4,5) */
    config |= 0x03;   /* Enable IRQ1 (keyboard) + IRQ12 (mouse) */
    config &= ~0x30;  /* Enable clock for both ports */

    /* Write updated configuration */
    ps2_send_cmd(0x60);
    ps2_send_data(config);

    /* Send 0xD4 to route next byte to mouse */
    ps2_send_cmd(0xD4);
    ps2_send_data(0xF4); /* Enable data reporting */

    /* Wait for and drain the ACK byte (0xFA) from the mouse.
     * If we don't read it here, it sits in the PS/2 output buffer
     * and blocks all keyboard data until IRQ 12 is delivered. */
    ps2_wait_read();
    inb(PS2_DATA_PORT);  /* discard ACK */

    /* Drain any additional response bytes (QEMU 10.1.3 may send extras) */
    ps2_flush();

    /* Register IRQ 12 handler (INT 44) — do this AFTER draining all
     * response bytes so the handler doesn't see stale ACK data */
    isr_register_handler(32 + MOUSE_IRQ, mouse_handler);

    /* Unmask cascade (IRQ 2) so slave PIC can deliver IRQ 12 */
    pic_clear_mask(2);
    pic_clear_mask(MOUSE_IRQ);

    /* Register /dev/mouse */
    device_register("mouse", mouse_dev_read, mouse_dev_write);

    debug_printf("mouse: PS/2 mouse initialized on IRQ %d\n", (uint64_t)MOUSE_IRQ);
}

int mouse_read_event(struct mouse_event *ev) {
    if (ring_head == ring_tail) return 0;
    *ev = ring[ring_tail];
    ring_tail = (ring_tail + 1) % RING_SIZE;
    return 1;
}
