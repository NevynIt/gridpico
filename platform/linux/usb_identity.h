#ifndef GRIDPICO_LINUX_USB_IDENTITY_H
#define GRIDPICO_LINUX_USB_IDENTITY_H

#include "api/linux_midi.h"
#include <sys/types.h>

typedef struct {
    dev_t filesystem;
    ino_t inode;
} linux_usb_node;

typedef struct {
    linux_usb_node device;
    linux_usb_node interface;
} linux_usb_location;

bool linux_usb_find(int sysfs, const gridpico_linux_usb_selector *selector,
                    linux_usb_location *out);
/* One means the card belongs to the interface, zero means a different card,
 * and minus one means the candidate could not be checked. */
int linux_usb_card_matches(int sysfs, int card, linux_usb_node interface,
                           linux_usb_node *out_card);
bool linux_usb_same_node(linux_usb_node left, linux_usb_node right);

#endif
