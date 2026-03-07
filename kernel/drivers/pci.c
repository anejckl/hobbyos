#include "pci.h"
#include "../string.h"
#include "../debug/debug.h"

static struct pci_device pci_devices[PCI_MAX_DEVICES];
static uint32_t pci_device_count = 0;

uint32_t pci_read_config(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) {
    uint32_t address = (1U << 31)
        | ((uint32_t)bus << 16)
        | ((uint32_t)device << 11)
        | ((uint32_t)func << 8)
        | ((uint32_t)(offset & 0xFC));
    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (1U << 31)
        | ((uint32_t)bus << 16)
        | ((uint32_t)device << 11)
        | ((uint32_t)func << 8)
        | ((uint32_t)(offset & 0xFC));
    outl(PCI_CONFIG_ADDR, address);
    outl(PCI_CONFIG_DATA, value);
}

void pci_enable_bus_mastering(struct pci_device *dev) {
    uint32_t cmd = pci_read_config(dev->bus, dev->device, dev->function, 0x04);
    cmd |= (1 << 2); /* Bus Master bit */
    pci_write_config(dev->bus, dev->device, dev->function, 0x04, cmd);
}

static void pci_scan_device(uint8_t bus, uint8_t device, uint8_t func) {
    uint32_t reg0 = pci_read_config(bus, device, func, 0x00);
    uint16_t vendor = (uint16_t)(reg0 & 0xFFFF);
    uint16_t dev_id = (uint16_t)(reg0 >> 16);

    if (vendor == 0xFFFF)
        return;

    if (pci_device_count >= PCI_MAX_DEVICES)
        return;

    struct pci_device *d = &pci_devices[pci_device_count];
    d->bus = bus;
    d->device = device;
    d->function = func;
    d->vendor_id = vendor;
    d->device_id = dev_id;
    d->present = true;

    uint32_t reg2 = pci_read_config(bus, device, func, 0x08);
    d->class_code = (uint8_t)(reg2 >> 24);
    d->subclass = (uint8_t)(reg2 >> 16);

    for (int i = 0; i < 6; i++)
        d->bar[i] = pci_read_config(bus, device, func, 0x10 + (uint8_t)(i * 4));

    uint32_t reg_int = pci_read_config(bus, device, func, 0x3C);
    d->irq_line = (uint8_t)(reg_int & 0xFF);

    debug_printf("PCI: %x:%x.%x vendor=%x device=%x class=%x sub=%x irq=%u\n",
                 (uint64_t)bus, (uint64_t)device, (uint64_t)func,
                 (uint64_t)vendor, (uint64_t)dev_id,
                 (uint64_t)d->class_code, (uint64_t)d->subclass,
                 (uint64_t)d->irq_line);

    pci_device_count++;
}

void pci_init(void) {
    pci_device_count = 0;
    memset(pci_devices, 0, sizeof(pci_devices));

    for (uint8_t device = 0; device < 32; device++) {
        for (uint8_t func = 0; func < 8; func++) {
            pci_scan_device(0, device, func);
            /* If function 0 is not multi-function, skip 1-7 */
            if (func == 0) {
                uint32_t header = pci_read_config(0, device, 0, 0x0C);
                if (!((header >> 16) & 0x80))
                    break;
            }
        }
    }

    debug_printf("PCI: %u devices found\n", (uint64_t)pci_device_count);
}

struct pci_device *pci_find_device(uint16_t vendor, uint16_t device_id) {
    for (uint32_t i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor &&
            pci_devices[i].device_id == device_id)
            return &pci_devices[i];
    }
    return NULL;
}
