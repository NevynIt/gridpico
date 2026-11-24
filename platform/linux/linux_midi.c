#define _POSIX_C_SOURCE 200809L

#include "api/linux_midi.h"
#include "usb_identity.h"

#include <alsa/asoundlib.h>
#include <alloca.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    ALSA_INPUT_BUDGET = 16,
    ALSA_ADDRESS_SPACE = SND_SEQ_ADDRESS_UNKNOWN * (UINT8_MAX + 1)
};

typedef struct {
    gridpico_linux_midi_address address;
    linux_usb_location location;
    linux_usb_node card;
} usb_observation;

typedef struct {
    snd_seq_t *seq;
    snd_seq_addr_t remote;
    snd_seq_addr_t local;
    gridpico_patch_direction direction;
    gridpico_connection connection;
    bool faulted;
    bool fault_pending;
    gridpico_linux_midi_event_kind fault;
    /* Zero serial size denotes a manual ALSA address rather than a USB bind. */
    gridpico_linux_usb_selector selector;
    usb_observation usb;
} linux_binding;

struct gridpico_linux_midi {
    snd_seq_t *inventory;
    snd_midi_event_t *codec;
    linux_binding bindings[GRIDPICO_LOCAL_PORTS];
    size_t cursor;
};

static bool valid_message(const uint8_t *bytes)
{
    if (!bytes || bytes[1] > 127 || bytes[2] > 127) {
        return false;
    }
    const uint8_t kind = bytes[0] >> 4;
    return (kind >= 8 && kind <= 11) || kind == 14;
}

static bool same_address(snd_seq_addr_t left, snd_seq_addr_t right)
{
    return left.client == right.client && left.port == right.port;
}

static bool supports(unsigned int caps, gridpico_patch_direction direction)
{
    const unsigned int required = direction == GRIDPICO_PATCH_INPUT
        ? SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ
        : SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE;
    return (caps & required) == required;
}

static linux_binding *binding_for(gridpico_linux_midi *midi, gridpico_connection connection)
{
    for (size_t i = 0; i < GRIDPICO_LOCAL_PORTS; ++i) {
        linux_binding *binding = &midi->bindings[i];
        if (binding->connection.endpoint == connection.endpoint) {
            return binding;
        }
    }
    return NULL;
}

static void remember_fault(linux_binding *binding, gridpico_linux_midi_event_kind fault)
{
    binding->faulted = true;
    binding->fault_pending = true;
    binding->fault = fault;
}

gridpico_result gridpico_linux_midi_open(gridpico_linux_midi **out)
{
    if (!out) {
        return GRIDPICO_INVALID;
    }
    *out = NULL;
    gridpico_linux_midi *midi = calloc(1, sizeof *midi);
    if (!midi) {
        return GRIDPICO_CAPACITY;
    }
    if (snd_seq_open(&midi->inventory, "hw", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) {
        free(midi);
        return GRIDPICO_UNAVAILABLE;
    }
    if (snd_midi_event_new(GRIDPICO_LOCAL_MESSAGE_BYTES, &midi->codec) < 0) {
        snd_seq_close(midi->inventory);
        free(midi);
        return GRIDPICO_CAPACITY;
    }
    snd_midi_event_no_status(midi->codec, 1);
    *out = midi;
    return GRIDPICO_OK;
}

void gridpico_linux_midi_close(gridpico_linux_midi *midi)
{
    if (!midi) {
        return;
    }
    for (size_t i = 0; i < GRIDPICO_LOCAL_PORTS; ++i) {
        if (midi->bindings[i].seq) {
            snd_seq_close(midi->bindings[i].seq);
        }
    }
    snd_midi_event_free(midi->codec);
    snd_seq_close(midi->inventory);
    free(midi);
}

static bool own_client(const gridpico_linux_midi *midi, int id)
{
    if (id == SND_SEQ_CLIENT_SYSTEM || id == snd_seq_client_id(midi->inventory)) {
        return true;
    }
    for (size_t i = 0; i < GRIDPICO_LOCAL_PORTS; ++i) {
        if (midi->bindings[i].seq && id == midi->bindings[i].local.client) {
            return true;
        }
    }
    return false;
}

gridpico_result gridpico_linux_midi_list(gridpico_linux_midi *midi,
                                        gridpico_linux_midi_port *ports,
                                        size_t capacity, size_t *count)
{
    if (!midi || !count || (capacity && !ports)) {
        return GRIDPICO_INVALID;
    }
    *count = 0;
    if (capacity > ALSA_ADDRESS_SPACE) {
        capacity = ALSA_ADDRESS_SPACE;
    }
    gridpico_linux_midi_port *snapshot = capacity ? calloc(capacity, sizeof *snapshot) : NULL;
    if (capacity && !snapshot) {
        return GRIDPICO_CAPACITY;
    }

    snd_seq_client_info_t *client;
    snd_seq_port_info_t *port;
    snd_seq_client_info_alloca(&client);
    snd_seq_port_info_alloca(&port);
    snd_seq_client_info_set_client(client, -1);
    size_t found = 0;
    int status;
    while ((status = snd_seq_query_next_client(midi->inventory, client)) >= 0) {
        const int id = snd_seq_client_info_get_client(client);
        if (id < 0 || id >= SND_SEQ_ADDRESS_UNKNOWN || own_client(midi, id)) {
            continue;
        }
        snd_seq_port_info_set_client(port, id);
        snd_seq_port_info_set_port(port, -1);
        while ((status = snd_seq_query_next_port(midi->inventory, port)) >= 0) {
            const unsigned int caps = snd_seq_port_info_get_capability(port);
            const bool read = supports(caps, GRIDPICO_PATCH_INPUT);
            const bool write = supports(caps, GRIDPICO_PATCH_OUTPUT);
            if (!read && !write) {
                continue;
            }
            if (found < capacity) {
                gridpico_linux_midi_port *row = &snapshot[found];
                row->address.client = (uint8_t)id;
                row->address.port = (uint8_t)snd_seq_port_info_get_port(port);
                row->can_read = read;
                row->can_write = write;
                snprintf(row->name, sizeof row->name, "%s", snd_seq_port_info_get_name(port));
            }
            ++found;
        }
        if (status != -ENOENT) {
            break;
        }
    }

    gridpico_result result = GRIDPICO_UNAVAILABLE;
    if (status == -ENOENT) {
        *count = found;
        result = GRIDPICO_OK;
        if (capacity && found > capacity) {
            result = GRIDPICO_CAPACITY;
        } else if (capacity && found) {
            memcpy(ports, snapshot, found * sizeof *ports);
        }
    }
    free(snapshot);
    return result;
}

static int subscription_status(const linux_binding *binding)
{
    snd_seq_port_subscribe_t *subscription;
    snd_seq_port_subscribe_alloca(&subscription);
    const bool input = binding->direction == GRIDPICO_PATCH_INPUT;
    snd_seq_port_subscribe_set_sender(subscription, input ? &binding->remote : &binding->local);
    snd_seq_port_subscribe_set_dest(subscription, input ? &binding->local : &binding->remote);
    return snd_seq_get_port_subscription(binding->seq, subscription);
}

static bool valid_selector(const gridpico_linux_usb_selector *selector)
{
    return selector && selector->serial_size > 0 &&
           selector->serial_size <= GRIDPICO_LINUX_USB_SERIAL_BYTES;
}

static bool same_selector(const gridpico_linux_usb_selector *left,
                          const gridpico_linux_usb_selector *right)
{
    return left->vendor_id == right->vendor_id && left->product_id == right->product_id &&
           left->interface_number == right->interface_number &&
           left->serial_size == right->serial_size &&
           memcmp(left->serial, right->serial, left->serial_size) == 0;
}

static bool same_usb_observation(const usb_observation *left, const usb_observation *right)
{
    return left->address.client == right->address.client &&
           left->address.port == right->address.port &&
           linux_usb_same_node(left->card, right->card) &&
           linux_usb_same_node(left->location.device, right->location.device) &&
           linux_usb_same_node(left->location.interface, right->location.interface);
}

static bool observe_usb_once(snd_seq_t *seq, int sysfs,
                             const gridpico_linux_usb_selector *selector,
                             gridpico_patch_direction direction, usb_observation *out)
{
    snd_seq_client_info_t *client;
    snd_seq_port_info_t *port;
    snd_seq_client_info_alloca(&client);
    snd_seq_port_info_alloca(&port);
    /* A self PID verifies that the running kernel supplies the card field. */
    if (snd_seq_get_client_info(seq, client) < 0 ||
        snd_seq_client_info_get_pid(client) != getpid() ||
        !linux_usb_find(sysfs, selector, &out->location)) {
        return false;
    }
    snd_seq_client_info_set_client(client, -1);
    bool found = false;
    int status;
    while ((status = snd_seq_query_next_client(seq, client)) >= 0) {
        const int id = snd_seq_client_info_get_client(client);
        const int card = snd_seq_client_info_get_card(client);
        if (id <= SND_SEQ_CLIENT_SYSTEM || id >= SND_SEQ_ADDRESS_UNKNOWN ||
            snd_seq_client_info_get_type(client) != SND_SEQ_KERNEL_CLIENT || card < 0) {
            continue;
        }
        linux_usb_node card_node;
        const int match = linux_usb_card_matches(sysfs, card, out->location.interface, &card_node);
        if (match < 0) {
            return false;
        }
        if (!match) {
            continue;
        }
        snd_seq_port_info_set_client(port, id);
        snd_seq_port_info_set_port(port, -1);
        while ((status = snd_seq_query_next_port(seq, port)) >= 0) {
            const unsigned int required = SND_SEQ_PORT_TYPE_HARDWARE | SND_SEQ_PORT_TYPE_MIDI_GENERIC;
            if ((snd_seq_port_info_get_type(port) & required) != required ||
                !supports(snd_seq_port_info_get_capability(port), direction)) {
                continue;
            }
            if (found) {
                return false;
            }
            out->address = (gridpico_linux_midi_address){
                (uint8_t)id, (uint8_t)snd_seq_port_info_get_port(port)
            };
            out->card = card_node;
            found = true;
        }
        if (status != -ENOENT) {
            return false;
        }
    }
    return found && status == -ENOENT;
}

static bool observe_usb(gridpico_linux_midi *midi, const gridpico_linux_usb_selector *selector,
                        gridpico_patch_direction direction, usb_observation *out)
{
    const int sysfs = open("/sys", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (sysfs < 0) {
        return false;
    }
    usb_observation prior;
    const bool verified = observe_usb_once(midi->inventory, sysfs, selector, direction, &prior) &&
                          observe_usb_once(midi->inventory, sysfs, selector, direction, out) &&
                          same_usb_observation(&prior, out);
    close(sysfs);
    return verified;
}

gridpico_result gridpico_linux_midi_resolve_usb(
    gridpico_linux_midi *midi, const gridpico_linux_usb_selector *selector,
    gridpico_patch_direction direction, gridpico_linux_midi_address *out_address)
{
    if (!midi || !valid_selector(selector) || !out_address ||
        (direction != GRIDPICO_PATCH_INPUT && direction != GRIDPICO_PATCH_OUTPUT)) {
        return GRIDPICO_INVALID;
    }
    usb_observation current;
    if (!observe_usb(midi, selector, direction, &current)) {
        return GRIDPICO_UNAVAILABLE;
    }
    *out_address = current.address;
    return GRIDPICO_OK;
}

gridpico_result gridpico_linux_midi_bind(gridpico_linux_midi *midi,
                                        gridpico_linux_midi_address address,
                                        gridpico_patch_direction direction,
                                        gridpico_connection connection)
{
    if (!midi || !connection.endpoint || !connection.generation ||
        (direction != GRIDPICO_PATCH_INPUT && direction != GRIDPICO_PATCH_OUTPUT) ||
        address.client >= SND_SEQ_ADDRESS_UNKNOWN) {
        return GRIDPICO_INVALID;
    }
    if (own_client(midi, address.client)) {
        return GRIDPICO_UNAVAILABLE;
    }

    linux_binding *slot = binding_for(midi, connection);
    if (slot && (slot->seq || connection.generation <= slot->connection.generation)) {
        return GRIDPICO_UNAVAILABLE;
    }
    const snd_seq_addr_t remote = { .client = address.client, .port = address.port };
    for (size_t i = 0; i < GRIDPICO_LOCAL_PORTS; ++i) {
        linux_binding *binding = &midi->bindings[i];
        if (binding->seq && binding->direction == direction && same_address(binding->remote, remote)) {
            return GRIDPICO_UNAVAILABLE;
        }
        if (!slot && !binding->connection.endpoint) {
            slot = binding;
        }
    }
    if (!slot) {
        return GRIDPICO_CAPACITY;
    }

    snd_seq_port_info_t *port_info;
    snd_seq_port_info_alloca(&port_info);
    if (snd_seq_get_any_port_info(midi->inventory, address.client, address.port, port_info) < 0 ||
        !supports(snd_seq_port_info_get_capability(port_info), direction)) {
        return GRIDPICO_UNAVAILABLE;
    }

    /* One input queue per binding. Closing it discards events from that generation. */
    snd_seq_t *seq = NULL;
    if (snd_seq_open(&seq, "hw", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) {
        return GRIDPICO_UNAVAILABLE;
    }
    if (snd_seq_set_client_name(seq, "Gridpico MIDI") < 0) {
        snd_seq_close(seq);
        return GRIDPICO_UNAVAILABLE;
    }
    unsigned int caps = SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE |
                        SND_SEQ_PORT_CAP_NO_EXPORT;
    if (direction == GRIDPICO_PATCH_OUTPUT) {
        caps |= SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;
    }
    const int local_port = snd_seq_create_simple_port(
        seq, direction == GRIDPICO_PATCH_INPUT ? "Gridpico input" : "Gridpico output",
        caps, SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (local_port < 0 ||
        snd_seq_connect_from(seq, local_port, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE) < 0) {
        snd_seq_close(seq);
        return GRIDPICO_UNAVAILABLE;
    }
    const int status = direction == GRIDPICO_PATCH_INPUT
        ? snd_seq_connect_from(seq, local_port, address.client, address.port)
        : snd_seq_connect_to(seq, local_port, address.client, address.port);
    if (status < 0) {
        snd_seq_close(seq);
        return GRIDPICO_UNAVAILABLE;
    }
    *slot = (linux_binding){
        .seq = seq,
        .remote = remote,
        .local = { .client = (uint8_t)snd_seq_client_id(seq), .port = (uint8_t)local_port },
        .direction = direction,
        .connection = connection
    };
    return GRIDPICO_OK;
}

gridpico_result gridpico_linux_midi_bind_usb(
    gridpico_linux_midi *midi, const gridpico_linux_usb_selector *selector,
    gridpico_patch_direction direction, gridpico_connection connection,
    gridpico_linux_midi_address *out_address)
{
    if (!midi || !valid_selector(selector) || !connection.endpoint ||
        !connection.generation || !out_address ||
        (direction != GRIDPICO_PATCH_INPUT && direction != GRIDPICO_PATCH_OUTPUT)) {
        return GRIDPICO_INVALID;
    }
    /* ALSA cannot confirm which subscription accepted a written event.
     * Keep verified USB outputs pending until held-note delivery is safe. */
    if (direction == GRIDPICO_PATCH_OUTPUT) {
        return GRIDPICO_UNAVAILABLE;
    }
    usb_observation prior;
    if (!observe_usb(midi, selector, direction, &prior)) {
        return GRIDPICO_UNAVAILABLE;
    }
    linux_binding *old = binding_for(midi, connection);
    const linux_binding previous = old ? *old : (linux_binding){0};
    const gridpico_result result = gridpico_linux_midi_bind(midi, prior.address, direction, connection);
    if (result != GRIDPICO_OK) {
        return result;
    }
    linux_binding *binding = binding_for(midi, connection);
    usb_observation current;
    if (!observe_usb(midi, selector, direction, &current) ||
        !same_usb_observation(&prior, &current) || subscription_status(binding) < 0) {
        snd_seq_close(binding->seq);
        *binding = previous;
        return GRIDPICO_UNAVAILABLE;
    }
    binding->selector = *selector;
    binding->usb = current;
    *out_address = current.address;
    return GRIDPICO_OK;
}

gridpico_result gridpico_linux_midi_check_usb_binding(
    gridpico_linux_midi *midi, const gridpico_linux_usb_selector *selector,
    gridpico_connection connection)
{
    if (!midi || !valid_selector(selector) || !connection.endpoint || !connection.generation) {
        return GRIDPICO_INVALID;
    }
    linux_binding *binding = binding_for(midi, connection);
    usb_observation current;
    if (!binding || !binding->seq || binding->faulted ||
        binding->direction != GRIDPICO_PATCH_INPUT ||
        binding->connection.generation != connection.generation ||
        !same_selector(&binding->selector, selector) ||
        !observe_usb(midi, selector, binding->direction, &current) ||
        !same_usb_observation(&binding->usb, &current) || subscription_status(binding) < 0) {
        return GRIDPICO_UNAVAILABLE;
    }
    return GRIDPICO_OK;
}

gridpico_result gridpico_linux_midi_unbind(gridpico_linux_midi *midi, gridpico_connection connection)
{
    if (!midi || !connection.endpoint || !connection.generation) {
        return GRIDPICO_INVALID;
    }
    linux_binding *binding = binding_for(midi, connection);
    if (!binding || !binding->seq || binding->connection.generation != connection.generation) {
        return GRIDPICO_UNAVAILABLE;
    }
    const int status = snd_seq_close(binding->seq);
    *binding = (linux_binding){ .connection = connection };
    return status < 0 ? GRIDPICO_UNAVAILABLE : GRIDPICO_OK;
}

gridpico_result gridpico_linux_midi_send(gridpico_linux_midi *midi,
                                        gridpico_connection destination,
                                        const uint8_t bytes[GRIDPICO_LOCAL_MESSAGE_BYTES])
{
    if (!midi || !valid_message(bytes) || !destination.endpoint || !destination.generation) {
        return GRIDPICO_INVALID;
    }
    linux_binding *binding = binding_for(midi, destination);
    if (!binding || !binding->seq || binding->faulted ||
        binding->direction != GRIDPICO_PATCH_OUTPUT ||
        binding->connection.generation != destination.generation) {
        return GRIDPICO_UNAVAILABLE;
    }

    snd_seq_event_t event;
    snd_seq_ev_clear(&event);
    snd_midi_event_reset_encode(midi->codec);
    if (snd_midi_event_encode(midi->codec, bytes, GRIDPICO_LOCAL_MESSAGE_BYTES, &event) !=
        GRIDPICO_LOCAL_MESSAGE_BYTES || event.type == SND_SEQ_EVENT_NONE) {
        return GRIDPICO_INVALID;
    }
    const int subscribed = subscription_status(binding);
    if (subscribed < 0) {
        remember_fault(binding, subscribed == -ENOENT
            ? GRIDPICO_LINUX_MIDI_SUBSCRIPTION_REMOVED : GRIDPICO_LINUX_MIDI_IO_FAILED);
        return GRIDPICO_UNAVAILABLE;
    }
    snd_seq_ev_set_source(&event, binding->local.port);
    snd_seq_ev_set_dest(&event, binding->remote.client, binding->remote.port);
    snd_seq_ev_set_direct(&event);
    const int status = snd_seq_event_output_direct(binding->seq, &event);
    if (status == -EAGAIN || status == -ENOMEM || status == -ENOSPC) {
        return GRIDPICO_CAPACITY;
    }
    if (status < 0) {
        remember_fault(binding, GRIDPICO_LINUX_MIDI_IO_FAILED);
        return GRIDPICO_UNAVAILABLE;
    }
    return GRIDPICO_OK;
}

static void check_announcement(linux_binding *binding, const snd_seq_event_t *event)
{
    if ((event->type == SND_SEQ_EVENT_PORT_EXIT && same_address(event->data.addr, binding->remote)) ||
        (event->type == SND_SEQ_EVENT_CLIENT_EXIT && event->data.addr.client == binding->remote.client)) {
        remember_fault(binding, GRIDPICO_LINUX_MIDI_PORT_REMOVED);
    } else if (event->type == SND_SEQ_EVENT_PORT_UNSUBSCRIBED) {
        const bool input = binding->direction == GRIDPICO_PATCH_INPUT;
        const snd_seq_addr_t sender = input ? binding->remote : binding->local;
        const snd_seq_addr_t receiver = input ? binding->local : binding->remote;
        if (same_address(event->data.connect.sender, sender) &&
            same_address(event->data.connect.dest, receiver)) {
            remember_fault(binding, GRIDPICO_LINUX_MIDI_SUBSCRIPTION_REMOVED);
        }
    }
}

static bool next_binding_event(gridpico_linux_midi *midi, linux_binding *binding,
                               gridpico_linux_midi_event *out)
{
    for (size_t i = 0; !binding->faulted && i < ALSA_INPUT_BUDGET; ++i) {
        snd_seq_event_t *event;
        const int status = snd_seq_event_input(binding->seq, &event);
        if (status == -EAGAIN || status == -EINTR) {
            break;
        }
        if (status < 0) {
            remember_fault(binding, GRIDPICO_LINUX_MIDI_IO_FAILED);
            break;
        }
        if (!same_address(event->dest, binding->local)) {
            continue;
        }
        if (event->source.client == SND_SEQ_CLIENT_SYSTEM &&
            event->source.port == SND_SEQ_PORT_SYSTEM_ANNOUNCE) {
            check_announcement(binding, event);
        } else if (binding->direction == GRIDPICO_PATCH_INPUT &&
                   same_address(event->source, binding->remote)) {
            uint8_t bytes[GRIDPICO_LOCAL_MESSAGE_BYTES];
            snd_midi_event_reset_decode(midi->codec);
            if (snd_midi_event_decode(midi->codec, bytes, sizeof bytes, event) == sizeof bytes &&
                valid_message(bytes)) {
                *out = (gridpico_linux_midi_event){
                    .kind = GRIDPICO_LINUX_MIDI_RECEIVED,
                    .connection = binding->connection
                };
                memcpy(out->bytes, bytes, sizeof bytes);
                return true;
            }
        }
    }
    if (!binding->fault_pending) {
        return false;
    }
    *out = (gridpico_linux_midi_event){ .kind = binding->fault, .connection = binding->connection };
    binding->fault_pending = false;
    return true;
}

gridpico_result gridpico_linux_midi_next(gridpico_linux_midi *midi, gridpico_linux_midi_event *out,
                                        bool *has_event)
{
    if (!midi || !out || !has_event) {
        return GRIDPICO_INVALID;
    }
    *has_event = false;
    for (size_t i = 0; i < GRIDPICO_LOCAL_PORTS; ++i) {
        linux_binding *binding = &midi->bindings[midi->cursor];
        midi->cursor = (midi->cursor + 1) % GRIDPICO_LOCAL_PORTS;
        if (binding->seq && next_binding_event(midi, binding, out)) {
            *has_event = true;
            break;
        }
    }
    return GRIDPICO_OK;
}
