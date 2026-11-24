#define _POSIX_C_SOURCE 200809L

#include "usb_identity.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int open_directory(int parent, const char *name)
{
    return openat(parent, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
}

static bool read_node(int fd, linux_usb_node *out)
{
    struct stat status;
    if (fstat(fd, &status) < 0 || !S_ISDIR(status.st_mode)) {
        return false;
    }
    *out = (linux_usb_node){ status.st_dev, status.st_ino };
    return true;
}

bool linux_usb_same_node(linux_usb_node left, linux_usb_node right)
{
    return left.filesystem == right.filesystem && left.inode == right.inode;
}

/* Sysfs gives a newline-terminated value. Keep all bytes preceding it. */
static int read_value(int path, const char *name, uint8_t *out, size_t capacity)
{
    const int fd = openat(path, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return -errno;
    }
    size_t used = 0;
    while (used < capacity) {
        const ssize_t got = read(fd, out + used, capacity - used);
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got < 0) {
            const int error = errno;
            close(fd);
            return -error;
        }
        if (got == 0) {
            break;
        }
        used += (size_t)got;
    }
    close(fd);
    if (used == capacity) {
        return -E2BIG;
    }
    if (!used || out[used - 1] != '\n') {
        return -EINVAL;
    }
    return (int)used - 1;
}

static int hex_attribute(int path, const char *name, int digits)
{
    uint8_t bytes[6];
    if (read_value(path, name, bytes, sizeof bytes) != digits) {
        return -1;
    }
    int value = 0;
    for (int i = 0; i < digits; ++i) {
        const uint8_t ch = bytes[i];
        if (ch >= '0' && ch <= '9') {
            value = value * 16 + ch - '0';
        } else if (ch >= 'a' && ch <= 'f') {
            value = value * 16 + ch - 'a' + 10;
        } else if (ch >= 'A' && ch <= 'F') {
            value = value * 16 + ch - 'A' + 10;
        } else {
            return -1;
        }
    }
    return value;
}

static int device_match(int device, const gridpico_linux_usb_selector *selector)
{
    const int vendor = hex_attribute(device, "idVendor", 4);
    const int product = hex_attribute(device, "idProduct", 4);
    if (vendor < 0 || product < 0) {
        return -1;
    }
    if (vendor != selector->vendor_id || product != selector->product_id) {
        return 0;
    }
    uint8_t serial[GRIDPICO_LINUX_USB_SERIAL_BYTES + 2];
    const int size = read_value(device, "serial", serial, sizeof serial);
    if (size == -ENOENT || size == -E2BIG) {
        return 0;
    }
    if (size < 0) {
        return -1;
    }
    return size == selector->serial_size && memcmp(serial, selector->serial, (size_t)size) == 0;
}

static struct dirent *next_entry(DIR *dir)
{
    errno = 0;
    return readdir(dir);
}

static bool sole_midi_interface(int device, const char *device_name, uint8_t number,
                                linux_usb_node *out)
{
    const int scan = open_directory(device, ".");
    DIR *dir = scan < 0 ? NULL : fdopendir(scan);
    if (!dir) {
        if (scan >= 0) {
            close(scan);
        }
        return false;
    }
    bool found = false;
    bool complete = true;
    const size_t name_size = strlen(device_name);
    for (struct dirent *entry; (entry = next_entry(dir));) {
        if (strncmp(entry->d_name, device_name, name_size) ||
            entry->d_name[name_size] != ':') {
            continue;
        }
        const int iface = open_directory(device, entry->d_name);
        if (iface < 0) {
            complete = false;
            break;
        }
        const int usb_class = hex_attribute(iface, "bInterfaceClass", 2);
        const int subclass = hex_attribute(iface, "bInterfaceSubClass", 2);
        if (usb_class < 0 || subclass < 0) {
            complete = false;
        } else if (usb_class == 1 && subclass == 3) {
            complete = !found && hex_attribute(iface, "bInterfaceNumber", 2) == number &&
                       read_node(iface, out);
            found = true;
        }
        close(iface);
        if (!complete) {
            break;
        }
    }
    complete = complete && errno == 0;
    closedir(dir);
    return complete && found;
}

bool linux_usb_find(int sysfs, const gridpico_linux_usb_selector *selector,
                    linux_usb_location *out)
{
    const int scan = open_directory(sysfs, "bus/usb/devices");
    DIR *dir = scan < 0 ? NULL : fdopendir(scan);
    if (!dir) {
        if (scan >= 0) {
            close(scan);
        }
        return false;
    }
    bool found = false;
    bool complete = true;
    for (struct dirent *entry; (entry = next_entry(dir));) {
        /* USB interface aliases contain a colon. Match devices globally first. */
        if (entry->d_name[0] == '.' || strchr(entry->d_name, ':')) {
            continue;
        }
        const int device = open_directory(dirfd(dir), entry->d_name);
        if (device < 0) {
            complete = false;
            break;
        }
        const int match = device_match(device, selector);
        if (match < 0) {
            complete = false;
        } else if (match > 0) {
            complete = !found && read_node(device, &out->device) &&
                       sole_midi_interface(device, entry->d_name, selector->interface_number,
                                           &out->interface);
            found = true;
        }
        close(device);
        if (!complete) {
            break;
        }
    }
    complete = complete && errno == 0;
    closedir(dir);
    return complete && found;
}

int linux_usb_card_matches(int sysfs, int card, linux_usb_node interface,
                           linux_usb_node *out_card)
{
    char path[48];
    snprintf(path, sizeof path, "class/sound/card%d", card);
    const int card_fd = open_directory(sysfs, path);
    if (card_fd < 0) {
        return -1;
    }
    const int device_fd = open_directory(card_fd, "device");
    linux_usb_node observed;
    int result = -1;
    if (device_fd >= 0) {
        if (read_node(device_fd, &observed)) {
            result = linux_usb_same_node(observed, interface) ? (read_node(card_fd, out_card) ? 1 : -1) : 0;
        }
        close(device_fd);
    }
    close(card_fd);
    return result;
}
