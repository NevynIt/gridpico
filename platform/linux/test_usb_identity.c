#define _POSIX_C_SOURCE 200809L
#include "api/linux_midi.h"

#include <alsa/asoundlib.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *sysfs_fixture;
static bool extra_output;
static bool omit_hardware;
static bool move_on_subscription;
static bool subscription_live;
static int active_port = 7;
static int closed_handles;
static char fake_handle;

int __real_open(const char *path, int flags, ...);
int __wrap_open(const char *path, int flags, ...)
{
    assert(!(flags & O_CREAT));
    return __real_open(strcmp(path, "/sys") == 0 ? sysfs_fixture : path, flags);
}

int __wrap_snd_seq_open(snd_seq_t **handle, const char *name, int streams, int mode)
{
    (void)name;
    (void)streams;
    (void)mode;
    *handle = (snd_seq_t *)&fake_handle;
    return 0;
}

int __wrap_snd_seq_close(snd_seq_t *handle)
{
    assert(handle == (snd_seq_t *)&fake_handle);
    ++closed_handles;
    subscription_live = false;
    return 0;
}

int __wrap_snd_seq_client_id(snd_seq_t *handle)
{
    assert(handle == (snd_seq_t *)&fake_handle);
    return 128;
}

int __wrap_snd_seq_set_client_name(snd_seq_t *handle, const char *name)
{
    assert(handle == (snd_seq_t *)&fake_handle && name);
    return 0;
}

int __wrap_snd_seq_get_client_info(snd_seq_t *handle, snd_seq_client_info_t *info)
{
    snd_seq_client_info_set_client(info, __wrap_snd_seq_client_id(handle));
    return 0;
}

int __wrap_snd_seq_client_info_get_pid(const snd_seq_client_info_t *info)
{
    assert(snd_seq_client_info_get_client(info) == 128);
    return (int)getpid();
}

int __wrap_snd_seq_query_next_client(snd_seq_t *handle, snd_seq_client_info_t *info)
{
    assert(handle == (snd_seq_t *)&fake_handle);
    const int id = snd_seq_client_info_get_client(info);
    if (id >= 22) {
        return -ENOENT;
    }
    snd_seq_client_info_set_client(info, id < 20 ? 20 : id + 1);
    return 0;
}

snd_seq_client_type_t __wrap_snd_seq_client_info_get_type(const snd_seq_client_info_t *info)
{
    return snd_seq_client_info_get_client(info) == 21 ? SND_SEQ_USER_CLIENT : SND_SEQ_KERNEL_CLIENT;
}

int __wrap_snd_seq_client_info_get_card(const snd_seq_client_info_t *info)
{
    return snd_seq_client_info_get_client(info) == 22 ? 4 : 3;
}

static void set_port(snd_seq_port_info_t *info, int port, bool readable)
{
    const unsigned int input = SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;
    const unsigned int output = SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE;
    snd_seq_port_info_set_capability(info, output | (readable ? input : 0));
    snd_seq_port_info_set_type(info, SND_SEQ_PORT_TYPE_MIDI_GENERIC |
                              (omit_hardware ? 0 : SND_SEQ_PORT_TYPE_HARDWARE));
    snd_seq_port_info_set_port(info, port);
}

int __wrap_snd_seq_query_next_port(snd_seq_t *handle, snd_seq_port_info_t *info)
{
    assert(handle == (snd_seq_t *)&fake_handle);
    /* Port 21's userspace client and card 4 must never reach enumeration. */
    assert(snd_seq_port_info_get_client(info) == 20);
    const int current = snd_seq_port_info_get_port(info);
    if (current == UINT8_MAX) {
        set_port(info, active_port, true);
        return 0;
    }
    if (current == active_port && extra_output) {
        set_port(info, current + 1, false);
        return 0;
    }
    return -ENOENT;
}

int __wrap_snd_seq_get_any_port_info(snd_seq_t *handle, int client, int port, snd_seq_port_info_t *info)
{
    assert(handle == (snd_seq_t *)&fake_handle && client == 20 && port == active_port);
    set_port(info, port, true);
    return 0;
}

int __wrap_snd_seq_create_simple_port(snd_seq_t *handle, const char *name,
                                    unsigned int caps, unsigned int type)
{
    assert(handle == (snd_seq_t *)&fake_handle && name && caps && type);
    return 0;
}

int __wrap_snd_seq_connect_from(snd_seq_t *handle, int local, int client, int port)
{
    assert(handle == (snd_seq_t *)&fake_handle && local == 0);
    if (client == SND_SEQ_CLIENT_SYSTEM && port == SND_SEQ_PORT_SYSTEM_ANNOUNCE) {
        return 0;
    }
    assert(client == 20 && port == active_port);
    subscription_live = true;
    if (move_on_subscription) {
        ++active_port;
    }
    return 0;
}

int __wrap_snd_seq_get_port_subscription(snd_seq_t *handle, snd_seq_port_subscribe_t *subscription)
{
    assert(handle == (snd_seq_t *)&fake_handle);
    const snd_seq_addr_t *sender = snd_seq_port_subscribe_get_sender(subscription);
    const snd_seq_addr_t *receiver = snd_seq_port_subscribe_get_dest(subscription);
    assert(sender->client == 20 && sender->port == active_port &&
           receiver->client == 128 && receiver->port == 0);
    return subscription_live ? 0 : -ENOENT;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    sysfs_fixture = argv[1];
    gridpico_linux_midi *midi;
    assert(gridpico_linux_midi_open(&midi) == GRIDPICO_OK);
    const gridpico_linux_usb_selector selector = {
        .vendor_id = 0x1234, .product_id = 0xabcd, .interface_number = 1,
        .serial_size = 4, .serial = {'G', 'P', '4', '2'}
    };
    gridpico_linux_midi_address address = { 99, 99 };
    if (strcmp(argv[2], "unavailable") == 0) {
        assert(gridpico_linux_midi_resolve_usb(midi, &selector, GRIDPICO_PATCH_OUTPUT,
                                              &address) == GRIDPICO_UNAVAILABLE);
        assert(address.client == 99 && address.port == 99);
        gridpico_linux_midi_close(midi);
        return 0;
    }

    assert(gridpico_linux_midi_resolve_usb(midi, &selector, GRIDPICO_PATCH_OUTPUT,
                                          &address) == GRIDPICO_OK);
    assert(address.client == 20 && address.port == 7);
    extra_output = true;
    assert(gridpico_linux_midi_resolve_usb(midi, &selector, GRIDPICO_PATCH_OUTPUT,
                                          &address) == GRIDPICO_UNAVAILABLE);
    assert(gridpico_linux_midi_resolve_usb(midi, &selector, GRIDPICO_PATCH_INPUT,
                                          &address) == GRIDPICO_OK);
    extra_output = false;
    omit_hardware = true;
    assert(gridpico_linux_midi_resolve_usb(midi, &selector, GRIDPICO_PATCH_INPUT,
                                          &address) == GRIDPICO_UNAVAILABLE);
    omit_hardware = false;
    const gridpico_connection connection = { .endpoint = 5, .generation = 2 };
    address = (gridpico_linux_midi_address){ 99, 99 };
    assert(gridpico_linux_midi_bind_usb(midi, &selector, GRIDPICO_PATCH_OUTPUT, connection,
                                        &address) == GRIDPICO_UNAVAILABLE);
    assert(closed_handles == 0 && !subscription_live &&
           address.client == 99 && address.port == 99);
    move_on_subscription = true;
    assert(gridpico_linux_midi_bind_usb(midi, &selector, GRIDPICO_PATCH_INPUT, connection,
                                        &address) == GRIDPICO_UNAVAILABLE);
    assert(closed_handles == 1 && address.client == 99 && address.port == 99);
    move_on_subscription = false;
    --active_port;
    assert(gridpico_linux_midi_bind_usb(midi, &selector, GRIDPICO_PATCH_INPUT, connection,
                                        &address) == GRIDPICO_OK);
    assert(address.client == 20 && address.port == 7);
    assert(gridpico_linux_midi_check_usb_binding(midi, &selector, connection) == GRIDPICO_OK);
    const gridpico_connection stale = { .endpoint = 5, .generation = 1 };
    assert(gridpico_linux_midi_check_usb_binding(midi, &selector, stale) == GRIDPICO_UNAVAILABLE);
    subscription_live = false;
    assert(gridpico_linux_midi_check_usb_binding(midi, &selector, connection) == GRIDPICO_UNAVAILABLE);
    assert(gridpico_linux_midi_unbind(midi, connection) == GRIDPICO_OK);
    gridpico_linux_midi_close(midi);
}
