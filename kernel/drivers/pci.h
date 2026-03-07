#ifndef PCI_H
#define PCI_H

#include "../common.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define PCI_MAX_DEVICES 32

struct pci_device {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint32_t bar[6];
    uint8_t irq_line;
    bool present;
};

void pci_init(void);
struct pci_device *pci_find_device(uint16_t vendor, uint16_t device_id);
uint32_t pci_read_config(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
void pci_write_config(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value);
void pci_enable_bus_mastering(struct pci_device *dev);

#endif /* PCI_H */
