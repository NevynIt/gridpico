#include "app/rp2040_router.h"
#ifdef GRIDPICO_RP2040_PICO7_NORMAL
#include "app/rp2040_auto_routes.h"
#include "app/rp2040_pat2_apply.h"
#endif
#include "frontends/apc/apc_chooser.h"
#include "frontends/cli/cli_frontend.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CFG_TUH_MIDI 7
#define CFG_TUH_DEVICE_MAX 7
#define CFG_TUH_HUB 2
#define CFG_TUH_ENUMERATION_BUFSIZE 512
#define CFG_TUH_MIDI_EP_BUFSIZE 64

enum {
    HOST_ROOT = 1, NO_INTERFACE = UINT8_MAX, TX_TIMEOUT_US = 250000,
    TUSB_DESC_CONFIGURATION = 2, TUSB_DESC_INTERFACE = 4, TUSB_DESC_ENDPOINT = 5,
    TUSB_DESC_CS_INTERFACE = 0x24, TUSB_DESC_CS_ENDPOINT = 0x25,
    TUSB_CLASS_AUDIO = 1, AUDIO_SUBCLASS_MIDI_STREAMING = 3,
    TUSB_XFER_BULK = 2, TUSB_XFER_INTERRUPT = 3, TUSB_DIR_OUT = 0, TUSB_DIR_IN = 1,
    ROOT_HUB = 8, DOWNSTREAM_HUB = 9, PIPE_LIMIT = 32,
};
typedef unsigned tusb_dir_t;
typedef enum { XFER_RESULT_SUCCESS } xfer_result_t;

typedef struct __attribute__((packed)) {
    uint8_t bLength, bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces, configuration, string, attributes, power;
} tusb_desc_configuration_t;
typedef struct {
    uint8_t bLength, type, bInterfaceNumber, bAlternateSetting, bNumEndpoints,
            bInterfaceClass, bInterfaceSubClass, protocol, string;
} tusb_desc_interface_t;
typedef struct {
    uint8_t bLength, type, bEndpointAddress;
    struct { uint8_t xfer; } bmAttributes;
    uint8_t packet_size, packet_size_high, interval;
} tusb_desc_endpoint_t;
typedef struct {
    const tusb_desc_interface_t *desc_midi;
    const tusb_desc_endpoint_t *desc_epin, *desc_epout;
    const uint8_t *desc_header;
    uint16_t desc_midi_total_len;
} tuh_midi_descriptor_cb_t;
typedef struct { uint8_t daddr, bInterfaceNumber, rx_cable_count, tx_cable_count; } tuh_midi_mount_cb_t;
typedef struct { uint16_t bcdDevice; } tusb_desc_device_t;
typedef struct { uint8_t rhport, hub_addr, hub_port; } tuh_bus_info_t;
typedef struct { uint8_t daddr, ep_addr; size_t buflen; uint8_t *buffer; } tuh_xfer_t;

static gridpico_pico_router router;
static uint8_t host_endpoints_out[CFG_TUH_MIDI];
static bool mounted[DOWNSTREAM_HUB + 1], transfers[DOWNSTREAM_HUB + 1];
static tuh_bus_info_t bus[DOWNSTREAM_HUB + 1];
static bool stale_event;
static unsigned capacity_reports, aborts, completions;
static struct {
    gridpico_pico_packet packet;
    uint8_t address, endpoint;
    unsigned count;
} wire;

/* One modeled PIO entry per exact host address and endpoint, sharing EP0
 * across the two directions. Only the control address can be unassigned. */
static struct {
    uint8_t address, endpoint;
    bool open;
} pipes[PIPE_LIMIT];
static unsigned pipe_count, max_pipes;

static void open_pipe(uint8_t address, uint8_t endpoint)
{
    assert(address <= DOWNSTREAM_HUB && (address != 0 || (endpoint & 0x7fu) == 0));
    if ((endpoint & 0x7fu) == 0) endpoint = 0;
    size_t free_slot = PIPE_LIMIT;
    for (size_t i = 0; i < PIPE_LIMIT; ++i) {
        if (!pipes[i].open) {
            if (free_slot == PIPE_LIMIT) free_slot = i;
        } else if (pipes[i].address == address && pipes[i].endpoint == endpoint) {
            return;
        }
    }
    assert(free_slot < PIPE_LIMIT);
    pipes[free_slot].address = address;
    pipes[free_slot].endpoint = endpoint;
    pipes[free_slot].open = true;
    if (++pipe_count > max_pipes) max_pipes = pipe_count;
}

static void close_pipes(uint8_t address)
{
    for (size_t i = 0; i < PIPE_LIMIT; ++i) {
        if (pipes[i].open && pipes[i].address == address) {
            pipes[i].open = false;
            --pipe_count;
        }
    }
}

static bool pipe_open(uint8_t address, uint8_t endpoint)
{
    for (size_t i = 0; i < PIPE_LIMIT; ++i) {
        if (pipes[i].open && pipes[i].address == address && pipes[i].endpoint == endpoint) return true;
    }
    return false;
}

static bool tuh_descriptor_get_device_local(uint8_t address, tusb_desc_device_t *out)
{
    if (address == 0 || address > CFG_TUH_DEVICE_MAX || !mounted[address]) return false;
    *out = (tusb_desc_device_t){.bcdDevice = 0x0112};
    return true;
}
static bool tuh_vid_pid_get(uint8_t address, uint16_t *vid, uint16_t *pid)
{
    assert(address > 0 && address <= CFG_TUH_DEVICE_MAX && mounted[address]);
    *vid = address == 1 ? 0x09e8 : 0x1209;
    *pid = address == 1 ? 0x004e : 0x0001;
    return true;
}
static bool tuh_bus_info_get(uint8_t address, tuh_bus_info_t *out)
{
    if (address > DOWNSTREAM_HUB || !mounted[address]) return false;
    *out = bus[address];
    return true;
}
static bool tuh_mounted(uint8_t address) { return address <= DOWNSTREAM_HUB && mounted[address]; }
static uint16_t tu_le16toh(uint16_t value) { return value; }
static bool tuh_midi_mounted(uint8_t index) { return mounted[index + 1]; }
static uint8_t tu_edpt_dir(uint8_t address) { return address >> 7; }
static uint8_t tu_edpt_number(uint8_t address) { return address & 0xfu; }
static uint16_t tu_edpt_packet_size(const tusb_desc_endpoint_t *ep) { return ep->packet_size; }
static uint32_t save_and_disable_interrupts(void) { return 0; }
static void restore_interrupts(uint32_t irq) { assert(irq == 0); }
static bool tuh_task_event_ready(void) { return stale_event; }
static uint32_t tuh_midi_read_available(uint8_t index) { assert(tuh_midi_mounted(index)); return 0; }
static uint64_t time_us_64(void) { return 1000; }
static bool tuh_edpt_abort_xfer(uint8_t address, uint8_t endpoint)
{
    assert(pipe_open(address, endpoint) && endpoint == 2 && transfers[address]);
    transfers[address] = false;
    ++aborts;
    return true;
}
static bool tuh_edpt_xfer(tuh_xfer_t *transfer)
{
    const uint8_t address = transfer->daddr;
    assert(address > 0 && address <= CFG_TUH_DEVICE_MAX && mounted[address]);
    assert(pipe_open(address, transfer->ep_addr) && transfer->ep_addr == 2 && !transfers[address]);
    assert(transfer->buflen == sizeof wire.packet.bytes);
    transfers[address] = true;
    memcpy(wire.packet.bytes, transfer->buffer, transfer->buflen);
    wire.address = address;
    wire.endpoint = transfer->ep_addr;
    ++wire.count;
    return true;
}
static bool __real_midih_xfer_cb(uint8_t address, uint8_t endpoint, xfer_result_t result, uint32_t count)
{
    assert(address > 0 && address <= CFG_TUH_DEVICE_MAX && endpoint == 2);
    assert(result == XFER_RESULT_SUCCESS && count == GRIDPICO_PICO_PACKET_BYTES);
    if (stale_event) {
        assert(!transfers[address]);
        stale_event = false;
        return false;
    }
    assert(mounted[address] && transfers[address]);
    transfers[address] = false;
    ++completions;
    return true;
}

static void add_trace(char kind, uint8_t index, const uint8_t *bytes, size_t size)
{
    (void)index;
    if (kind == 'E') {
        assert(size == 1 && bytes[0] == GRIDPICO_CAPACITY);
        ++capacity_reports;
    }
}

#include "firmware.inc"

_Static_assert(GRIDPICO_PICO_USB_CABLES == 2 &&
               GRIDPICO_PATCH_SOURCE_ROWS == 5 && GRIDPICO_PATCH_DESTINATION_COLUMNS == 8 &&
               GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES == 53, "stock routing geometry");
_Static_assert(sizeof router.notes[0].packets == 512, "128 four-byte packets per ring");
#if defined(GRIDPICO_PICO7_HOST_FIXTURE) || defined(GRIDPICO_RP2040_PICO7_NORMAL)
_Static_assert(GRIDPICO_PICO_USB_DEVICES == 7 && GRIDPICO_PICO_USB_CABLE_GROUPS == 8 &&
               GRIDPICO_PATCH_MAX_DEVICES == 9 && GRIDPICO_PATCH_MAX_PORTS == 19, "Pico7 ABI");
_Static_assert(sizeof router.notes / sizeof router.notes[0] == 9, "Pico7 rings");
#else
_Static_assert(GRIDPICO_PICO_USB_DEVICES == 4 && GRIDPICO_PICO_USB_CABLE_GROUPS == 5 &&
               GRIDPICO_PATCH_MAX_DEVICES == 8 && GRIDPICO_PATCH_MAX_PORTS == 18, "stock ABI");
_Static_assert(sizeof router.notes / sizeof router.notes[0] == 6, "stock rings");
#endif

static void mount_hub(uint8_t address, uint8_t parent, uint8_t port)
{
    assert(!mounted[address] && (parent == 0 || mounted[parent]));
    bus[address] = (tuh_bus_info_t){HOST_ROOT, parent, port};
    open_pipe(address, 0);
    open_pipe(address, 0x80);
    open_pipe(address, 0x81);
    mounted[address] = true;
}

static void append_bytes(uint8_t **cursor, const uint8_t *bytes, size_t count)
{
    memcpy(*cursor, bytes, count);
    *cursor += count;
}

static void mount_midi(uint8_t index, const char *name, char serial)
{
    const uint8_t address = index + 1;
    const uint8_t parent = index < 4 ? ROOT_HUB : DOWNSTREAM_HUB;
    const uint8_t port = index < 4 ? (uint8_t)(index + 1) : (uint8_t)(index - 3);
    assert(mounted[parent] && !mounted[address]);
    bus[address] = (tuh_bus_info_t){HOST_ROOT, parent, port};

    const uint8_t cables = index == 0 ? 2u : 1u;
    struct {
        tusb_desc_configuration_t config;
        uint8_t data[180];
    } descriptors = {
        .config = {.bLength = 9, .bDescriptorType = TUSB_DESC_CONFIGURATION,
                   .bNumInterfaces = 2, .configuration = 1, .attributes = 0x80, .power = 50},
    };
    uint8_t *cursor = descriptors.data;
    const uint8_t audio_control[] = {
        9, TUSB_DESC_INTERFACE, 0, 0, 0, TUSB_CLASS_AUDIO, 1, 0, 0,
        9, TUSB_DESC_CS_INTERFACE, 1, 0, 1, 9, 0, 1, 1,
    };
    append_bytes(&cursor, audio_control, sizeof audio_control);
    const uint8_t *midi = cursor;
    const uint8_t endpoint_length = (uint8_t)(9 + 4 + cables);
    const uint8_t streaming[] = {
        9, TUSB_DESC_INTERFACE, 1, 0, 2, TUSB_CLASS_AUDIO, AUDIO_SUBCLASS_MIDI_STREAMING, 0, 0,
        7, TUSB_DESC_CS_INTERFACE, 1, 0, 1, (uint8_t)(7 + 30 * cables + 2 * endpoint_length), 0,
    };
    append_bytes(&cursor, streaming, sizeof streaming);
    for (uint8_t cable = 0; cable < cables; ++cable) {
        const uint8_t id = (uint8_t)(1 + 4 * cable);
        const uint8_t jacks[] = {
            6, TUSB_DESC_CS_INTERFACE, 2, 1, id, 0,
            6, TUSB_DESC_CS_INTERFACE, 2, 2, (uint8_t)(id + 1), 0,
            9, TUSB_DESC_CS_INTERFACE, 3, 1, (uint8_t)(id + 2), 1, (uint8_t)(id + 1), 1, 0,
            9, TUSB_DESC_CS_INTERFACE, 3, 2, (uint8_t)(id + 3), 1, id, 1, 0,
        };
        append_bytes(&cursor, jacks, sizeof jacks);
    }
    const uint8_t *out = cursor;
    const uint8_t out_endpoint[] = {9, TUSB_DESC_ENDPOINT, 2, TUSB_XFER_BULK, 64, 0, 0, 0, 0,
                                    (uint8_t)(4 + cables), TUSB_DESC_CS_ENDPOINT, 1, cables, 1, 5};
    append_bytes(&cursor, out_endpoint, endpoint_length);
    const uint8_t *in = cursor;
    const uint8_t in_endpoint[] = {9, TUSB_DESC_ENDPOINT, 0x81, TUSB_XFER_BULK, 64, 0, 0, 0, 0,
                                   (uint8_t)(4 + cables), TUSB_DESC_CS_ENDPOINT, 1, cables, 3, 7};
    append_bytes(&cursor, in_endpoint, endpoint_length);
    descriptors.config.wTotalLength = (uint16_t)(cursor - descriptors.data + sizeof descriptors.config);
    const tuh_midi_descriptor_cb_t descriptor = {
        .desc_midi = (const tusb_desc_interface_t *)midi, .desc_header = midi + 9,
        .desc_epin = (const tusb_desc_endpoint_t *)in, .desc_epout = (const tusb_desc_endpoint_t *)out,
        .desc_midi_total_len = (uint16_t)(cursor - midi),
    };
    const tuh_midi_mount_cb_t mount = {
        .daddr = address, .bInterfaceNumber = 1, .rx_cable_count = cables, .tx_cable_count = cables,
    };

    open_pipe(0, 0);
    open_pipe(0, 0x80);
    assert(tuh_enum_descriptor_configuration_cb(address, 0, &descriptors.config));
    assert(device_streams[address] == 1);
    close_pipes(0);
    open_pipe(address, 0);
    open_pipe(address, 0x80);
    open_pipe(address, descriptor.desc_epin->bEndpointAddress);
    open_pipe(address, descriptor.desc_epout->bEndpointAddress);
    mounted[address] = true;
    tuh_midi_descriptor_cb(index, &descriptor);
    tuh_midi_mount_cb(index, &mount);
    tuh_mount_cb(address);
    assert(observed_interface(index));
    midi_path path;
    assert(observed_path(address, &path));
    assert(path.depth == (parent == ROOT_HUB ? 1 : 2) && path.ports[0] == port);
    if (path.depth == 2) assert(path.ports[1] == bus[DOWNSTREAM_HUB].hub_port);
    midi_identity identity;
    char label[GRIDPICO_PATCH_NAME_BYTES] = {0};
    assert(read_identity(index, &identity));
    assert(snprintf(label, sizeof label, "%s", name) > 0);
    identity.serial[0] = serial;
    remember_observation(index, &identity, label);
#ifdef GRIDPICO_RP2040_PICO7_NORMAL
    /* Strings are completed at the model boundary. Run the real census turn. */
    admit_without_strings();
#else
    admit_discovered();
    discovery_dirty = false;
#endif
}

static void unmount_midi(uint8_t index)
{
    tuh_umount_cb(index + 1);
    tuh_midi_umount_cb(index);
    close_pipes(index + 1);
    mounted[index + 1] = false;
}

static void place_after_roles(void)
{
    assert(!discovery_dirty && discovery.index == NO_INTERFACE);
    assert(sync_apc_roles(&physical_roles) == GRIDPICO_OK);
    nominate_controller(&physical_roles);
    uint32_t inputs = 0, outputs = 0;
    automatic_policy_blocks(&physical_roles, &inputs, &outputs);
    assert(inputs == 2 && outputs == 2);
    const gridpico_revision graph = router.graph.revision;
    assert(gridpico_pico_router_place_discovered(&router, graph, router.session.apc_layout_revision,
                                                inputs, outputs) == GRIDPICO_OK);
    assert(router.graph.revision == graph);
}

static gridpico_cli_frontend cli;
static gridpico_local_routing_snapshot routing_scratch;
#ifdef GRIDPICO_RP2040_ROUTING_PAGES
static gridpico_patch_routing_page cli_routing_page;

static void bind_fixture_pages(void)
{
    gridpico_cli_bind_routing_pages(&cli, &router, gridpico_pico_router_read_routing_metadata,
                                   gridpico_pico_router_read_routing_page,
                                   gridpico_pico_router_read_guarded_managed_route,
                                   gridpico_pico_router_read_managed_route, gridpico_pico_router_set_managed_route,
                                   &cli_routing_page);
}
#endif
static char reply[2300];
static size_t reply_size;

static void capture(void *context, const char *bytes, size_t size)
{
    assert(context == NULL && size < sizeof reply - reply_size);
    memcpy(reply + reply_size, bytes, size);
    reply_size += size;
    reply[reply_size] = '\0';
}

static gridpico_result run_command(const char *command)
{
    reply_size = 0;
    reply[0] = '\0';
    return gridpico_cli_execute_line(&cli, command, capture, NULL);
}

static void expect_name(const char *record, const char *name)
{
    assert(*record++ == '"');
    for (; *name != '\0'; ++name) {
        if (*name == '"' || *name == '\\') assert(*record++ == '\\');
        assert(*record++ == *name);
    }
    assert(strcmp(record, "\"") == 0);
}

static void paged_routing(void)
{
    const gridpico_patch_inventory *inventory = &router.graph.inventory;
    bool devices[GRIDPICO_PATCH_MAX_DEVICES + 1] = {0};
    bool ports[GRIDPICO_PATCH_MAX_PORTS + 1] = {0};
    size_t device_count = 0, port_count = 0, managed[2] = {0}, controls[2] = {0};
    size_t offset = 0, pages = 0, equal = 0;
    gridpico_revision revision = 0;
    do {
        if (pages++ == 0) assert(run_command("routing") == GRIDPICO_OK);
        else {
            char command[80];
            assert(snprintf(command, sizeof command, "routing %" PRIu64 " %zu", revision, offset) > 0);
            assert(run_command(command) == GRIDPICO_OK);
        }
        uint64_t page_revision;
        char next[24];
        assert(sscanf(reply, "ROUTING %" SCNu64 " %23s", &page_revision, next) == 2);
        if (offset == 0) revision = page_revision;
        assert(revision == page_revision && reply_size >= 4 && strcmp(reply + reply_size - 4, "OK\r\n") == 0);
        size_t records = 0;
        for (char *line = strtok(reply, "\r\n"); line != NULL; line = strtok(NULL, "\r\n")) {
            unsigned id, parent;
            char direction[8], capability[16], availability[16];
            int name_offset = 0;
            if (sscanf(line, "DEVICE %u %*s %*s %n", &id, &name_offset) == 1) {
                assert(id != 0 && id <= GRIDPICO_PATCH_MAX_DEVICES && !devices[id] && name_offset > 0);
                const gridpico_patch_device *device = NULL;
                for (size_t i = 0; i < inventory->device_count; ++i) {
                    if (inventory->devices[i].id == id) device = &inventory->devices[i];
                }
                assert(device != NULL && strcmp(device->name, "Audition") != 0);
                expect_name(line + name_offset, device->name);
                equal += strcmp(device->name, "Equal") == 0;
                devices[id] = true;
                ++device_count;
            } else if (sscanf(line, "BINDING %u %u %7s %15s %15s %n", &id, &parent,
                              direction, capability, availability, &name_offset) == 5) {
                assert(id != 0 && id <= GRIDPICO_PATCH_MAX_PORTS && !ports[id] && name_offset > 0);
                assert(parent <= GRIDPICO_PATCH_MAX_DEVICES && devices[parent]);
                const gridpico_patch_port *port = NULL;
                for (size_t i = 0; i < inventory->port_count; ++i) {
                    if (inventory->ports[i].id == id) port = &inventory->ports[i];
                }
                assert(port != NULL && port->device == parent && strcmp(port->name, "Audition") != 0);
                expect_name(line + name_offset, port->name);
                const bool out = strcmp(direction, "out") == 0;
                assert((out && port->direction == GRIDPICO_PATCH_OUTPUT) ||
                       (strcmp(direction, "in") == 0 && port->direction == GRIDPICO_PATCH_INPUT));
                assert(strcmp(availability, "in-use") == 0);
                if (port->patch_eligible) {
                    assert(strcmp(capability, "managed") == 0);
                    ++managed[out];
                } else {
                    assert(strcmp(capability, "panel") == 0);
                    assert(id == (out ? physical_roles.led : physical_roles.panel));
                    ++controls[out];
                }
                ports[id] = true;
                ++port_count;
            } else {
                assert(strncmp(line, "ROUTING ", 8) == 0 || strcmp(line, "OK") == 0);
                continue;
            }
            ++records;
        }
        assert(records > 0 && records <= GRIDPICO_LOCAL_ROUTING_PAGE_RECORDS);
        if (strcmp(next, "-") == 0) break;
        assert(strtoul(next, NULL, 10) == offset + records);
        offset += records;
    } while (true);
    assert(pages == 4 && device_count == 8 && port_count == 18 && equal == 2);
    assert(managed[0] == 8 && managed[1] == 8 && controls[0] == 1 && controls[1] == 1);
    puts("Routing: four coherent pages, 8 public devices, 18 exact names/bindings, 8 Managed per axis and Panel/LED");
}

static void input(uint8_t index, uint8_t cable, uint8_t status, uint8_t key, uint8_t velocity)
{
    const gridpico_pico_packet packet = {{(uint8_t)((cable << 4) | (status >> 4)), status, key, velocity}};
    receive_discovered_packet(mounted_binding(index), packet);
}

static void complete(uint8_t index)
{
    assert(__wrap_midih_xfer_cb(index + 1, 2, XFER_RESULT_SUCCESS, GRIDPICO_PICO_PACKET_BYTES));
}

static void verify_apc_led(void)
{
    unsigned frames = 0;
    while (gridpico_pico_router_led(&router, GRIDPICO_PICO_APC) != NULL) {
        assert(++frames <= 2 * GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES);
        const unsigned previous = wire.count;
        service_apc_led_out(0);
        assert(wire.count == previous + 1 && wire.address == 1 && wire.endpoint == 2);
        assert((wire.packet.bytes[0] >> 4) == 1);
        complete(0);
    }
    assert(frames >= GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES &&
           frames % GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES == 0);
    const gridpico_apc_physical *panel = &router.panels[GRIDPICO_PICO_APC].frontend;
    assert(panel->route_page_ready == !panel->arranging);
}

static bool queues_empty(void)
{
    for (unsigned peer = GRIDPICO_PICO_WEB; peer < GRIDPICO_PICO_PEERS; ++peer) {
        if (gridpico_pico_router_note(&router, (gridpico_pico_peer)peer) != NULL) return false;
    }
    return true;
}

static gridpico_revision routing_revision(void)
{
    assert(run_command("routing") == GRIDPICO_OK);
#ifdef GRIDPICO_RP2040_ROUTING_PAGES
    return cli_routing_page.metadata.revision;
#else
    return routing_scratch.revision;
#endif
}

static void route_command(gridpico_port_id source, gridpico_port_id destination,
                          gridpico_revision guard, const char *state)
{
    char command[96], record[96];
    if (guard != 0) {
        assert(snprintf(command, sizeof command, "route %u %u managed on if %" PRIu64,
                        (unsigned)source, (unsigned)destination, guard) > 0);
    } else {
        assert(snprintf(command, sizeof command, "route %u %u managed",
                        (unsigned)source, (unsigned)destination) > 0);
    }
    if (state == NULL) {
        assert(run_command(command) == GRIDPICO_UNAVAILABLE && strcmp(reply, "ERR UNAVAILABLE\r\n") == 0);
    } else {
        assert(snprintf(record, sizeof record, "ROUTE %u %u MANAGED %s\r\nOK\r\n",
                        (unsigned)source, (unsigned)destination, state) > 0);
        assert(run_command(command) == GRIDPICO_OK && strcmp(reply, record) == 0);
    }
}

#ifdef GRIDPICO_RP2040_ROUTING_PAGES
static gridpico_result refuse_full_vector(void *context, gridpico_local_graph_state *out)
{
    (void)context;
    (void)out;
    assert(!"Selected Routing or APC used the legacy full-vector callback");
    return GRIDPICO_INVALID;
}

static gridpico_local_route seed_routing_visibility_case(void)
{
    const gridpico_port_id audition = router.session.apc_audition_source;
    static gridpico_patch_graph_route_metadata raw;
    assert(gridpico_patch_graph_read_route_metadata(&router.graph, &raw) == GRIDPICO_OK);
    size_t count = raw.enabled_count;
    const size_t public_count = GRIDPICO_PATCH_MAX_BRANCHES - 1u;
    if (count > public_count) {
        gridpico_patch_graph_route_page first;
        assert(gridpico_patch_graph_read_route_page(&router.graph, raw.revision, 0, &first) == GRIDPICO_OK);
        const gridpico_local_route_request remove = {first.records[0].route, false};
        assert(gridpico_patch_graph_set_local_route(&router.graph, &remove) == GRIDPICO_OK);
        --count;
    }
    const gridpico_patch_inventory *inventory = &raw.registry.inventory;
    for (size_t i = 0; i < inventory->port_count && count < public_count; ++i) {
        const gridpico_patch_port *in = &inventory->ports[i];
        if (in->direction != GRIDPICO_PATCH_INPUT || !in->patch_eligible || in->id == audition) continue;
        for (size_t j = 0; j < inventory->port_count && count < public_count; ++j) {
            const gridpico_patch_port *out = &inventory->ports[j];
            if (out->direction != GRIDPICO_PATCH_OUTPUT || !out->patch_eligible) continue;
            const gridpico_local_route_request on = {{in->id, out->id, GRIDPICO_LOCAL_MANAGED}, true};
            gridpico_local_route_status status;
            assert(gridpico_patch_graph_read_local_route(&router.graph, &on.route, &status) == GRIDPICO_OK);
            if (status.state != GRIDPICO_PATCH_ROUTE_OFF) continue;
            assert(gridpico_patch_graph_set_local_route(&router.graph, &on) == GRIDPICO_OK);
            ++count;
        }
    }
    assert(count == public_count);
    const gridpico_patch_port_update live = {
        .connection = {audition, ++router.audition.source_generation}, .state = GRIDPICO_PATCH_CONNECTED,
    };
    const gridpico_local_route_request hidden = {
        {audition, router.discovered[0].output, GRIDPICO_LOCAL_MANAGED}, true,
    };
    assert(gridpico_patch_graph_update_port(&router.graph, &live) == GRIDPICO_OK);
    assert(gridpico_patch_graph_set_local_route(&router.graph, &hidden) == GRIDPICO_OK);
    gridpico_local_route_status status;
    assert(gridpico_patch_graph_read_local_route(&router.graph, &hidden.route, &status) == GRIDPICO_OK);
    assert(status.state == GRIDPICO_PATCH_ROUTE_ACTIVE);
    return hidden.route;
}

static void verify_routing_pages(void)
{
    const gridpico_local_route hidden = seed_routing_visibility_case();
    const gridpico_device_id color_device = router.graph.inventory.devices[2].id;
    assert(gridpico_session_set_device_color(&router.session, color_device, (gridpico_rgb8){0x12, 0x34, 0x56})
           == GRIDPICO_OK);
    const gridpico_revision revision = routing_revision();
    const gridpico_patch_routing_metadata *metadata = &cli_routing_page.metadata;
    assert(metadata->graph.enabled_count == GRIDPICO_PATCH_MAX_BRANCHES - 1u);
    const size_t devices = metadata->graph.registry.inventory.device_count;
    const size_t ports = metadata->graph.registry.inventory.port_count;
    assert(devices + 1u == router.graph.inventory.device_count && ports + 1u == router.graph.inventory.port_count);
    assert(router.session.routing_port_count == router.graph.inventory.port_count);
    assert(metadata->graph.registry.inventory.devices[1].id == color_device);
    assert(metadata->colors[1].is_set && metadata->colors[1].rgb.green == 0x34);

    const size_t total = devices + ports + metadata->graph.enabled_count;
    gridpico_local_read_graph_fn legacy_read = router.session.read_local_graph;
    char command[96];
    static char expected[sizeof reply];
    for (size_t offset = 0; offset <= total; ++offset) {
        assert(snprintf(command, sizeof command, "routing %" PRIu64 " %zu", revision, offset) > 0);
        router.session.read_local_graph = legacy_read;
        gridpico_cli_bind_routing(&cli, &router, gridpico_pico_router_read_routing,
                                 gridpico_pico_router_read_managed_route, gridpico_pico_router_set_managed_route,
                                 &routing_scratch);
        assert(run_command(command) == GRIDPICO_OK);
        memcpy(expected, reply, reply_size + 1u);
        router.session.read_local_graph = refuse_full_vector;
        bind_fixture_pages();
        assert(run_command(command) == GRIDPICO_OK && strcmp(reply, expected) == 0);
        assert(strstr(reply, "Audition") == NULL && cli_routing_page.metadata.revision == revision);
    }
    assert(cli_routing_page.record_count == 0 && cli_routing_page.next_offset == total);
    assert(snprintf(command, sizeof command, "routing %" PRIu64 " %zu", revision, total + 1u) > 0);
    assert(run_command(command) == GRIDPICO_INVALID && strcmp(reply, "ERR INVALID\r\n") == 0);

    const gridpico_revision graph = router.graph.revision;
    assert(gridpico_session_set_device_color(&router.session, color_device, (gridpico_rgb8){0x65, 0x43, 0x21})
           == GRIDPICO_OK && router.graph.revision == graph);
    assert(snprintf(command, sizeof command, "routing %" PRIu64 " 8", revision) > 0);
    assert(run_command(command) == GRIDPICO_UNAVAILABLE && strcmp(reply, "ERR UNAVAILABLE\r\n") == 0);

    gridpico_apc_physical *apc = &router.panels[GRIDPICO_PICO_APC].frontend;
    gridpico_apc_patch_snapshot *panel = &router.panel_scratch;
    assert(gridpico_patch_session_read_apc_patch(&router.session, panel) == GRIDPICO_OK);
    assert(panel->cells[0][0].compatible && panel->graph_revision == graph);
    const gridpico_local_route selected = {panel->layout.rows[0], panel->layout.columns[0], GRIDPICO_LOCAL_MANAGED};
    const gridpico_patch_route_state before = panel->cells[0][0].state;
    const bool was_on = before != GRIDPICO_PATCH_ROUTE_OFF;
    assert(apc->set_cell(apc->session_context, panel->layout_revision, panel->graph_revision, 0, 0, was_on)
           == GRIDPICO_OK && router.graph.revision == graph);

    const gridpico_revision fresh = routing_revision();
    assert(fresh != revision);
    gridpico_local_route_status refused = {.revision = 17, .state = GRIDPICO_PATCH_ROUTE_PENDING};
    assert(gridpico_pico_router_read_guarded_managed_route(&router, fresh, &hidden, &refused) == GRIDPICO_INVALID);
    assert(refused.revision == 17 && refused.state == GRIDPICO_PATCH_ROUTE_PENDING);
    assert(snprintf(command, sizeof command, "route %u %u managed off if %" PRIu64,
                    (unsigned)hidden.source, (unsigned)hidden.destination, fresh) > 0);
    assert(run_command(command) == GRIDPICO_INVALID && strcmp(reply, "ERR INVALID\r\n") == 0);
    assert(router.graph.transform_note == NULL && cli.apply_note_transform == NULL);
    assert(snprintf(command, sizeof command, "transform %u %u 1 0 if %" PRIu64,
                    (unsigned)selected.source, (unsigned)selected.destination, fresh) > 0);
    assert(run_command(command) == GRIDPICO_UNAVAILABLE && strcmp(reply, "ERR UNAVAILABLE\r\n") == 0);
    assert(snprintf(command, sizeof command, "route %u %u managed off if %" PRIu64,
                    (unsigned)selected.source, (unsigned)selected.destination, fresh) > 0);
    assert(run_command(command) == GRIDPICO_OK && strstr(reply, "MANAGED OFF\r\nOK\r\n") != NULL);
    route_command(selected.source, selected.destination, 0, "OFF");
    route_command(selected.source, selected.destination, revision, NULL);
    if (was_on) route_command(selected.source, selected.destination, routing_revision(),
                             before == GRIDPICO_PATCH_ROUTE_ACTIVE ? "ACTIVE" : "PENDING");
    router.session.read_local_graph = legacy_read;
    printf("PASS %u-slot Pico Routing, public offsets/legacy bytes, hidden active Audition, color stale, Off and APC\n",
           GRIDPICO_PATCH_MAX_BRANCHES);
}
#endif

static void submit_to(uint8_t target)
{
    const unsigned previous = wire.count;
    for (uint8_t i = 0; i < CFG_TUH_MIDI; ++i) {
        const discovered_device *binding = mounted_binding(i);
        assert((binding != NULL && service_musical_out(i, binding)) == (i == target));
    }
    assert(wire.count == previous + 1 && wire.address == target + 1 && wire.endpoint == 2);
    assert(current_musical_flight(target));
}

static void receive_exact(uint8_t target, uint8_t status, uint8_t key, uint8_t velocity)
{
    submit_to(target);
    const gridpico_pico_packet expected = {{(uint8_t)(status >> 4), status, key, velocity}};
    assert(memcmp(wire.packet.bytes, expected.bytes, sizeof expected.bytes) == 0);
    const unsigned previous = completions;
    complete(target);
    assert(completions == previous + 1 && queues_empty());
}

static void pair(uint8_t from, uint8_t target, uint8_t key)
{
    input(from, 0, 0x90, key, 0x60);
    receive_exact(target, 0x90, key, 0x60);
    input(from, 0, 0x80, key, 0x20);
    receive_exact(target, 0x80, key, 0x20);
}

static void expect_capacity(uint8_t index, const char *name, char serial)
{
    static gridpico_pico_router before;
    discovered_device bindings[GRIDPICO_PICO_USB_DEVICES];
    memcpy(&before, &router, sizeof before);
    memcpy(bindings, discovered, sizeof bindings);
    const unsigned previous = capacity_reports;
    mount_midi(index, name, serial);
    assert(observed_interface(index) && observations[index].verified);
    assert(observations[index].capacity_reported && capacity_reports == previous + 1);
    assert(mounted_binding(index) == NULL);
    assert(memcmp(&before, &router, sizeof before) == 0);
    assert(memcmp(bindings, discovered, sizeof bindings) == 0);
}

static const char *const names[] = {"APC", "Equal", "C \"quoted\"", "Equal", "E", "F", "G"};

#ifdef GRIDPICO_RP2040_PICO7_NORMAL
enum { SAVED_LAST_IN = 3, SAVED_LAST_OUT = 4 };
static uint8_t boot_sectors[GRIDPICO_PICO_SAVE_SECTORS][GRIDPICO_PICO_SAVE_SECTOR];
static uint8_t original_boot_sectors[sizeof boot_sectors];

static gridpico_pico_pat2_identity saved_identity(uint8_t serial)
{
    const bool apc = serial == 'A';
    return (gridpico_pico_pat2_identity){
        .vid = apc ? 0x09e8 : 0x1209, .pid = apc ? 0x004e : 0x0001, .bcd_device = 0x0112,
        .interface_number = 1, .input_endpoint = 0x81, .output_endpoint = 2,
        .input_cables = apc ? 3 : 1, .output_cables = apc ? 3 : 1,
        .serial_length = 1, .serial = {serial},
    };
}

#ifndef GRIDPICO_PICO25_HOST_FIXTURE
static void start_normal_boot(void)
{
    _Static_assert(GRIDPICO_PATCH_MAX_BRANCHES == 24, "installed Matrix-24 backing");
    const gridpico_pico_pat2_intent saved = {
        .generation = 5, .identity_count = 3, .direction_count = 4, .pair_count = 3,
        .identities = {saved_identity('A'), saved_identity('B'), saved_identity('G')},
        .directions = {
            {.identity_ref = 1, .direction = GRIDPICO_PATCH_INPUT, .label = "A"},
            {.identity_ref = 2, .direction = GRIDPICO_PATCH_OUTPUT, .label = "B"},
            {.identity_ref = 3, .direction = GRIDPICO_PATCH_INPUT, .label = "Late G IN"},
            {.identity_ref = 3, .direction = GRIDPICO_PATCH_OUTPUT, .label = "Late G OUT"},
        },
        .slots = {[0] = SAVED_LAST_IN, [GRIDPICO_PATCH_SOURCE_ROWS] = SAVED_LAST_OUT},
        .pairs = {{1, 2, 0}, {SAVED_LAST_IN, SAVED_LAST_OUT, 1}, {SAVED_LAST_IN, 2, 0}},
    };
    memset(boot_sectors, 0xff, sizeof boot_sectors);
    assert(gridpico_pico_encode_pat2(&saved, boot_sectors[1], sizeof boot_sectors[1]) == GRIDPICO_OK);
    memcpy(original_boot_sectors, boot_sectors, sizeof boot_sectors);
    pat2_boot_read(boot_sectors[0], boot_sectors[1]);
    assert(pat2_boot.read && pat2_boot.pending && pat2_boot.status.selected == 1);
    assert(router.pat2_apply == &pat2_boot.apply && pat2_boot.intent.generation == 5);
    pat2_boot_read(NULL, NULL);
    assert(pat2_boot.status.selected == 1 && pat2_boot.intent.generation == 5);
    gridpico_pico_auto_routes_init(&automatic_routes, &router, &routing_scratch);
}
#endif

static gridpico_patch_route_state route_state(gridpico_local_route route)
{
    gridpico_local_route_status status;
    assert(gridpico_patch_graph_read_local_route(&router.graph, &route, &status) == GRIDPICO_OK);
    return status.state;
}

static void seed_real_pairs(gridpico_local_route saved_off)
{
    gridpico_local_graph_state *state = &router.projection_scratch.graph;
    assert(gridpico_patch_graph_read_local_state(&router.graph, state) == GRIDPICO_OK);
    size_t count = state->route_count;
    const gridpico_patch_inventory *inventory = &router.graph.inventory;
    for (size_t i = 0; i < inventory->port_count && count < 23; ++i) {
        const gridpico_patch_port *in = &inventory->ports[i];
        if (!in->patch_eligible || in->direction != GRIDPICO_PATCH_INPUT ||
            in->id == router.session.apc_audition_source) continue;
        for (size_t j = 0; j < inventory->port_count && count < 23; ++j) {
            const gridpico_patch_port *out = &inventory->ports[j];
            if (!out->patch_eligible || out->direction != GRIDPICO_PATCH_OUTPUT ||
                (in->id == saved_off.source && out->id == saved_off.destination)) continue;
            const gridpico_local_route_request request = {{in->id, out->id, GRIDPICO_LOCAL_MANAGED}, true};
            if (route_state(request.route) != GRIDPICO_PATCH_ROUTE_OFF) continue;
            assert(gridpico_pico_router_commit_local_route(&router, &request) == GRIDPICO_OK);
            ++count;
        }
    }
    assert(gridpico_patch_graph_read_local_state(&router.graph, state) == GRIDPICO_OK);
    assert(count == 23 && state->route_count == 23);
}

static void check_full_projection(void)
{
    const gridpico_pico_pat2_verified_view *view = &pat2_boot.apply.projection.view;
    assert(view->observation_count == 7 && view->binding_count == 16 && view->branch_count == 24);
    size_t lane = 0;
    for (uint8_t index = 0; index < CFG_TUH_MIDI; ++index) {
        const discovered_device *device = mounted_binding(index);
        assert(view->observations[index].identity.serial_length == 1);
        assert(view->observations[index].identity.serial[0] == 'A' + index);
        for (uint8_t cable = 0; cable < cable_span(&device->identity); ++cable) {
            const unsigned peer = device->first_peer + cable;
            const size_t slot = peer - GRIDPICO_PICO_DISCOVERED;
            for (unsigned out = 0; out < 2; ++out) {
                const gridpico_pico_pat2_binding *binding = &view->bindings[lane++];
                assert(binding->observation_index == index && binding->cable == cable);
                assert(binding->direction == (out ? GRIDPICO_PATCH_OUTPUT : GRIDPICO_PATCH_INPUT));
                assert(binding->binding.endpoint == (out ? router.discovered[slot].output : router.discovered[slot].input));
                assert(binding->binding.generation == (out ? router.generation[peer] :
                                                        router.discovered[slot].input_generation));
                assert(binding->eligible == !(index == 0 && cable == 1));
            }
        }
    }
    assert(lane == view->binding_count);
}

static void expect_25th_refused(gridpico_port_id in, gridpico_port_id out)
{
    char command[96];
    const gridpico_revision guard = routing_revision();
    assert(snprintf(command, sizeof command, "route %u %u managed on if %" PRIu64,
                    (unsigned)in, (unsigned)out, guard) > 0);
    static gridpico_patch_graph graph_before;
    const gridpico_apc_patch_layout layout = router.session.apc_layout;
    const gridpico_patch_manual_routes off = router.manual_routes;
    const gridpico_pico_pat2_progress progress = pat2_boot.apply.progress;
    const gridpico_revision layout_revision = router.session.apc_layout_revision;
    assert(route_state((gridpico_local_route){in, out, GRIDPICO_LOCAL_MANAGED}) == GRIDPICO_PATCH_ROUTE_OFF);
    memcpy(&graph_before, &router.graph, sizeof graph_before);
    assert(run_command(command) == GRIDPICO_CAPACITY && strcmp(reply, "ERR CAPACITY\r\n") == 0);
    assert(memcmp(&graph_before, &router.graph, sizeof graph_before) == 0);
    assert(router.session.apc_layout_revision == layout_revision &&
           memcmp(&layout, &router.session.apc_layout, sizeof layout) == 0 &&
           memcmp(&off, &router.manual_routes, sizeof off) == 0 &&
           memcmp(&progress, &pat2_boot.apply.progress, sizeof progress) == 0 &&
           queues_empty() && wire.count == 0);
}

static void verify_pat2_refusals(void)
{
    gridpico_pico_pat2_apply *apply = &pat2_boot.apply;
    static gridpico_pico_router before;
    assert(sync_apc_roles(&physical_roles) == GRIDPICO_OK);
    nominate_controller(&physical_roles);
    memcpy(&before, &router, sizeof(before));
    const gridpico_pico_pat2_progress progress = apply->progress;
    const gridpico_revision roles = physical_roles.revision;
    for (unsigned probe = 0; probe < 4; ++probe) {
        assert(gridpico_pico_project_pat2_view(&apply->projection) == GRIDPICO_OK);
        assert(gridpico_pico_pat2_prepare(&router, roles) == GRIDPICO_OK);
        assert(router.graph.route_shadow_peer != 0 && apply->before.membership.enabled_count == 24 &&
               !apply->before.membership.present[1] && apply->before.membership.present[2] &&
               apply->next.pair_outcomes[1] == GRIDPICO_PAT2_APPLIED &&
               apply->next.pair_outcomes[2] == GRIDPICO_PAT2_APPLIED);
        assert(gridpico_pico_project_pat2_view(&apply->projection) == GRIDPICO_OK);
        switch (probe) {
        case 0:
            --apply->projection.view.branch_count;
            assert(gridpico_pico_pat2_commit(&router, roles) == GRIDPICO_UNAVAILABLE);
            break;
        case 1:
            apply->shadow.graph.revision = router.graph.revision - 1;
            assert(gridpico_pico_pat2_commit(&router, roles) == GRIDPICO_INVALID);
            break;
        case 2:
            assert(gridpico_pico_pat2_commit(&router, roles - 1) == GRIDPICO_UNAVAILABLE);
            break;
        case 3:
            gridpico_pico_pat2_discard(&router);
            assert(gridpico_pico_pat2_commit(&router, roles) == GRIDPICO_UNAVAILABLE);
            break;
        }
        assert(!apply->prepared && apply->shadow.owner_address == 0 &&
               memcmp(&before, &router, sizeof(before)) == 0 &&
               memcmp(&progress, &apply->progress, sizeof(progress)) == 0 &&
               memcmp(original_boot_sectors, boot_sectors, sizeof boot_sectors) == 0);
    }
    puts("PAT2 shadow: count mismatch, Graph publication refusal, stale roles and abandonment retain live bytes and sectors");
}

static void normal_boot_case(void)
{
    for (uint8_t i = 4; i < 6; ++i) {
        mount_midi(i, names[i], (char)('A' + i));
        assert(mounted_binding(i) != NULL);
    }
    const gridpico_local_route saved_off = {
        router.discovered[0].input, router.discovered[2].output, GRIDPICO_LOCAL_MANAGED,
    };
    const gridpico_local_route_request prior = {saved_off, true};
    assert(gridpico_pico_router_commit_local_route(&router, &prior) == GRIDPICO_OK);
    assert(route_state(saved_off) == GRIDPICO_PATCH_ROUTE_ACTIVE);
    service_automatic_policy();
    assert(physical_roles.panel == router.discovered[1].input && physical_roles.led == router.discovered[1].output);
    assert(pat2_boot.coherent && pat2_boot.pending);
    assert(pat2_boot.apply.progress.pair_outcomes[0] == GRIDPICO_PAT2_APPLIED &&
           pat2_boot.apply.progress.pair_outcomes[1] == GRIDPICO_PAT2_DUE);
    assert(pat2_boot.apply.progress.reservations[0] == SAVED_LAST_IN &&
           pat2_boot.apply.progress.reservations[GRIDPICO_PATCH_SOURCE_ROWS] == SAVED_LAST_OUT);
    assert(router.session.apc_layout.rows[0] == 0 && router.session.apc_layout.columns[0] == 0 &&
           route_state(saved_off) == GRIDPICO_PATCH_ROUTE_OFF);
    assert(automatic_routes.coherent && automatic_routes.next == 1 &&
           memcmp(&automatic_routes.manual_routes, &router.manual_routes, sizeof router.manual_routes) == 0);
    seed_real_pairs(saved_off);

    const gridpico_revision prior_epoch = routing_revision();
    mount_midi(6, names[6], 'G');
    const discovered_device *last = mounted_binding(6);
    assert(last != NULL);
    const size_t slot = last->first_peer - GRIDPICO_PICO_DISCOVERED;
    assert(slot == 7 && router.graph.inventory.device_count == 9 && router.graph.inventory.port_count == 19);
    const gridpico_local_route saved_on = {
        router.discovered[slot].input, router.discovered[slot].output, GRIDPICO_LOCAL_MANAGED,
    };
    assert(pat2_boot.discovery_changed && route_state(saved_on) == GRIDPICO_PATCH_ROUTE_OFF);
    const gridpico_local_route_request last_slot = {
        {saved_on.source, saved_off.destination, GRIDPICO_LOCAL_MANAGED}, true,
    };
    assert(gridpico_pico_router_commit_local_route(&router, &last_slot) == GRIDPICO_OK);
    assert(route_state(last_slot.route) == GRIDPICO_PATCH_ROUTE_ACTIVE);
    verify_pat2_refusals();

    service_automatic_policy();
    check_full_projection();
    assert(pat2_boot.apply.projection.view.branch_count == 24 && router.graph.route_shadow_peer == 0);
    assert(pat2_boot.coherent && !pat2_boot.pending &&
           pat2_boot.apply.progress.pair_outcomes[1] == GRIDPICO_PAT2_APPLIED &&
           pat2_boot.apply.progress.pair_outcomes[2] == GRIDPICO_PAT2_APPLIED);
    assert(router.session.apc_layout.rows[0] == saved_on.source &&
           router.session.apc_layout.columns[0] == saved_on.destination &&
           route_state(saved_on) == GRIDPICO_PATCH_ROUTE_ACTIVE);
    assert(automatic_routes.graph_revision == router.graph.revision &&
           router.projection_scratch.graph.route_count == 24);
    assert(route_state(saved_off) == GRIDPICO_PATCH_ROUTE_OFF &&
           route_state(last_slot.route) == GRIDPICO_PATCH_ROUTE_OFF);
    puts("PAT2 boot: saved Off before Auto, waiting On/slots resolved by seventh identity, exact 7 observations/16 directions");

    route_command(saved_on.source, saved_on.destination, prior_epoch, NULL);
    expect_25th_refused(saved_on.source, saved_off.destination);
    gridpico_auto_route_summary summary;
    gridpico_result read;
    unsigned turns = 0;
    do {
        assert(++turns <= 2 * GRIDPICO_AUTO_ROUTE_MAX_PAIRS + 2);
        service_automatic_policy();
        read = gridpico_pico_auto_routes_read(&automatic_routes, &summary);
    } while (read == GRIDPICO_UNAVAILABLE);
    assert(read == GRIDPICO_OK && summary.user_off == 2 && route_state(saved_off) == GRIDPICO_PATCH_ROUTE_OFF &&
           route_state(last_slot.route) == GRIDPICO_PATCH_ROUTE_OFF);
    assert(queues_empty() && wire.count == 0);
    puts("Matrix-24: late Off frees slot 24 for earlier-listed On, guarded slot 25 refused, no replay");

    pair(6, 6, 67);
    const unsigned sent = wire.count;
    const uint32_t lifetime = last->lifetime;
    const uint32_t generation = router.generation[last->first_peer];
    const uint32_t input_generation = router.discovered[slot].input_generation;
    unmount_midi(6);
    assert(route_state(saved_on) == GRIDPICO_PATCH_ROUTE_PENDING && queues_empty());
    mount_midi(6, names[6], 'G');
    service_automatic_policy();
    assert(mounted_binding(6) == last && last->lifetime > lifetime && router.generation[last->first_peer] > generation);
    assert(router.discovered[slot].input_generation > input_generation &&
           router.discovered[slot].input == saved_on.source &&
           router.discovered[slot].output == saved_on.destination);
    assert(route_state(saved_on) == GRIDPICO_PATCH_ROUTE_ACTIVE && queues_empty() && wire.count == sent);
    pair(6, 6, 68);
    assert(memcmp(original_boot_sectors, boot_sectors, sizeof boot_sectors) == 0);
    puts("Late epoch: old token refused, exact identity rebind, fresh selected Note On/Off and no replay");
    puts("PASS pico7_two_hub_admission --normal-boot");
}
#endif

#ifdef GRIDPICO_PICO25_HOST_FIXTURE
#include "pico25_host.inc"
int main(int argc, char **argv)
#else
int main(void)
#endif
{
    assert(gridpico_pico_router_init(&router) == GRIDPICO_OK && router.graph.branch_limit == GRIDPICO_PATCH_MAX_BRANCHES);
#ifdef GRIDPICO_PICO25_HOST_FIXTURE
    assert(argc == 3 && (strcmp(argv[1], "machine") == 0 || strcmp(argv[1], "boot") == 0));
    bitmap_case(strcmp(argv[1], "boot") == 0, argv[2]);
    return 0;
#elif defined(GRIDPICO_RP2040_PICO7_NORMAL)
    start_normal_boot();
#endif
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_WEB, true);
    assert(gridpico_cli_init(&cli, &router.session, gridpico_patch_session_read_snapshot, NULL, NULL,
                             NULL, NULL, capture, NULL) == GRIDPICO_OK);
#ifdef GRIDPICO_RP2040_ROUTING_PAGES
    bind_fixture_pages();
#else
    gridpico_cli_bind_routing(&cli, &router, gridpico_pico_router_read_routing,
                             gridpico_pico_router_read_managed_route, gridpico_pico_router_set_managed_route,
                             &routing_scratch);
#endif

    mount_hub(ROOT_HUB, 0, 0);
    mount_hub(DOWNSTREAM_HUB, ROOT_HUB, 5);
    for (uint8_t i = 0; i < 4; ++i) {
        mount_midi(i, names[i], (char)('A' + i));
        assert(mounted_binding(i) != NULL);
    }
#ifdef GRIDPICO_RP2040_PICO7_NORMAL
    normal_boot_case();
#ifdef GRIDPICO_RP2040_ROUTING_PAGES
    verify_routing_pages();
#endif
    return 0;
#endif
    place_after_roles();
    if (GRIDPICO_PICO_USB_DEVICES == 4) {
#if defined(GRIDPICO_RP2040_ROUTING_PAGES) && !defined(GRIDPICO_RP2040_PICO7_NORMAL)
        _Static_assert(GRIDPICO_PATCH_MAX_BRANCHES == 13, "use the exact 13-slot control header");
        verify_routing_pages();
#endif
        assert(router.graph.inventory.device_count == 6 && router.graph.inventory.port_count == 13);
        expect_capacity(4, names[4], 'E');
        assert(pipe_count == 19 && wire.count == 0);
        puts("stock: fifth valid class mount reports CAPACITY with unchanged router, Graph, placement and bindings");
        return 0;
    }

    for (uint8_t i = 4; i < CFG_TUH_MIDI; ++i) {
        mount_midi(i, names[i], (char)('A' + i));
        assert(mounted_binding(i) != NULL);
    }
    place_after_roles();
    assert(router.graph.inventory.device_count == 9 && router.graph.inventory.port_count == 19);
    assert(physical_roles.panel == router.discovered[1].input && physical_roles.led == router.discovered[1].output);
    assert(router.session.apc_layout.rows[4] != 0 && router.session.apc_layout.columns[6] != 0 &&
           router.session.apc_layout.columns[7] == 0);
    assert(router.apc_placement_end == 0 && queues_empty() && wire.count == 0);
    assert(pipe_count == 25 && max_pipes == 25);
    open_pipe(0, 0);
    open_pipe(0, 0x80);
    assert(pipe_count == 26 && max_pipes == 26);
    close_pipes(0);
    assert(pipe_count == 25);
    puts("Pico7: 7 identities, 8 duplex groups, 9 devices/19 directions; two hubs, 25 pipes plus shared transient EP0");
    paged_routing();
    verify_apc_led();
    for (unsigned toggle = 0; toggle < 2; ++toggle) {
        input(0, 1, 0x90, 0x51, 0x7f);
        input(0, 1, 0x80, 0x51, 0);
        assert(router.panels[GRIDPICO_PICO_APC].frontend.arranging == (toggle == 0));
        verify_apc_led();
    }
    assert(queues_empty() && pipe_count == 25);
    puts("APC: Panel on cable 1, real 53-frame LED completion on the same interface OUT");

    const unsigned a = mounted_binding(0)->first_peer - GRIDPICO_PICO_DISCOVERED;
    const unsigned b = mounted_binding(1)->first_peer - GRIDPICO_PICO_DISCOVERED;
    const unsigned g = mounted_binding(6)->first_peer - GRIDPICO_PICO_DISCOVERED;
    const gridpico_pico_peer g_peer = mounted_binding(6)->first_peer;
    const gridpico_port_id a_in = router.discovered[a].input, a_out = router.discovered[a].output;
    const gridpico_port_id b_in = router.discovered[b].input;
    const gridpico_port_id g_in = router.discovered[g].input, g_out = router.discovered[g].output;
    route_command(a_in, g_out, routing_revision(), "ACTIVE");
    route_command(b_in, a_out, routing_revision(), "ACTIVE");
    assert(queues_empty());
    pair(1, 0, 60);
    pair(0, 6, 67);
    puts("MIDI: fresh selected-only Note On/Off at early APC musical cable and seventh receiver");

    const uint32_t lifetime = mounted_binding(6)->lifetime;
    const uint32_t input_generation = router.discovered[g].input_generation;
    const uint32_t output_generation = router.generation[g_peer];
    const gridpico_revision old_revision = routing_revision();
    input(0, 0, 0x90, 70, 0x60);
    submit_to(6);
    input(0, 0, 0x80, 70, 0x20);
    assert(router.notes[g_peer - GRIDPICO_PICO_WEB].count == 2);
    unmount_midi(6);
    assert(aborts == 1 && mounted_binding(6) == NULL && !current_musical_flight(6) && pipe_count == 22);
    assert(queues_empty());
    route_command(a_in, g_out, 0, "PENDING");
    route_command(b_in, a_out, 0, "ACTIVE");
    pair(1, 0, 61);

    expect_capacity(6, "Another G", 'Z');
    assert(pipe_count == 25 && router.discovered[g].output == g_out && !router.attached[g_peer]);
    route_command(a_in, g_out, 0, "PENDING");
    unmount_midi(6);
    const unsigned sent = wire.count;
    mount_midi(6, names[6], 'G');
    place_after_roles();
    assert(mounted_binding(6)->first_peer == g_peer && mounted_binding(6)->lifetime > lifetime);
    assert(router.discovered[g].input_generation > input_generation && router.generation[g_peer] > output_generation);
    assert(router.discovered[g].input == g_in && router.discovered[g].output == g_out);
    assert(router.graph.inventory.device_count == 9 && router.graph.inventory.port_count == 19);
    assert(capacity_reports == 1 && pipe_count == 25 && max_pipes == 26);
    route_command(a_in, g_out, old_revision, NULL);
    route_command(a_in, g_out, 0, "ACTIVE");
    assert(queues_empty() && wire.count == sent);

    /* A pending old HCD completion must drain before the replacement's
       first claim. It cannot consume even byte-identical fresh work. */
    stale_event = true;
    input(0, 0, 0x90, 70, 0x60);
    assert(service_musical_out(6, mounted_binding(6)));
    assert(!interfaces[6].busy && wire.count == sent);
    const unsigned completed = completions;
    assert(!__wrap_midih_xfer_cb(7, 2, XFER_RESULT_SUCCESS, GRIDPICO_PICO_PACKET_BYTES));
    assert(!stale_event && completions == completed && !interfaces[6].busy && wire.count == sent);
    assert(router.notes[g_peer - GRIDPICO_PICO_WEB].count == 1);
    receive_exact(6, 0x90, 70, 0x60);
    input(0, 0, 0x80, 70, 0x20);
    receive_exact(6, 0x80, 70, 0x20);
    pair(1, 0, 62);
    puts("retire/rebind: sibling stayed live, different serial refused, exact IDs returned, stale completion fenced and no replay");
    puts("PASS pico7_two_hub_admission");
}
