#include "hub_trace.h"

#include "hardware/sync.h"
#include "host/hcd.h"
#include "host/usbh.h"
#include "host/hub.h"

#include <stdio.h>
#include <string.h>

enum { ROWS = 64, DATA = 10 };

typedef struct {
    char kind;
    uint8_t address;
    uint8_t bytes[DATA];
} hub_row;

static hub_row history[ROWS];
static hub_row failure;
static uint8_t count;
static uint16_t lost;
static bool has_failure;
static volatile uint8_t root_address;
static volatile uint8_t status_endpoint;
static volatile bool configured_descriptor_seen;
static volatile bool child_attach_seen;

enum descriptor_stage { DESC_IDLE, DESC_SETUP, DESC_IN, DESC_RECEIVED };
static volatile enum descriptor_stage child_descriptor_stage;
static const uint8_t *child_descriptor_buffer;

static uint8_t command, cursor, snapshot_count;
static uint16_t snapshot_lost;
static bool output, snapshot_failed;
static hub_row snapshot_failure;

static void remember_failure(hub_row row)
{
    if (!has_failure) {
        failure = row;
        has_failure = true;
    }
}

/* Writers run in foreground or PIO IRQ. Rows do not wrap. */
static hub_row *capture(hub_row row, bool failed)
{
    hub_row *entry = NULL;
    const uint32_t interrupts = save_and_disable_interrupts();
    if (count < ROWS) {
        entry = &history[count++];
        *entry = row;
    } else if (lost < UINT16_MAX) {
        ++lost;
    }
    if (failed) remember_failure(row);
    restore_interrupts(interrupts);
    return entry;
}

static void capture_admission(hub_row *entry, hub_row row, unsigned field, bool queued)
{
    /* The foreground CDC snapshot cannot run until this driver call returns. */
    row.bytes[field] = queued;
    const uint32_t interrupts = save_and_disable_interrupts();
    if (entry != NULL) entry->bytes[field] = queued;
    if (!queued) remember_failure(row);
    restore_interrupts(interrupts);
}

void gridpico_hub_trace_device(uint8_t address, uint8_t subclass, uint8_t protocol)
{
    if (address == 0 || (root_address != 0 && root_address != address)) return;
    root_address = address;
    capture((hub_row){'D', address, {subclass, protocol}}, false);
}

void gridpico_hub_trace_config(uint8_t address)
{
    if (address != 0 && address == root_address) configured_descriptor_seen = true;
}

static bool watched(uint8_t address)
{
    return configured_descriptor_seen && (address == root_address || (address == 0 && child_attach_seen));
}

uint16_t __real_hub_open(uint8_t root, uint8_t address, const tusb_desc_interface_t *descriptor,
                         uint16_t length);
bool __real_hcd_setup_send(uint8_t root, uint8_t address, const uint8_t setup[8]);
bool __real_hcd_edpt_xfer(uint8_t root, uint8_t address, uint8_t endpoint, uint8_t *buffer, uint16_t len);
void __real_hcd_event_handler(const hcd_event_t *event, bool in_isr);

uint16_t __wrap_hub_open(uint8_t root, uint8_t address, const tusb_desc_interface_t *descriptor,
                         uint16_t length)
{
    const uint16_t consumed = __real_hub_open(root, address, descriptor, length);
    if (!watched(address) || address == 0) return consumed;

    hub_row row = {'O', address, {root}};
    if (length >= sizeof(*descriptor)) {
        row.bytes[1] = descriptor->bInterfaceNumber;
        row.bytes[2] = descriptor->bInterfaceClass;
        row.bytes[3] = descriptor->bInterfaceSubClass;
        row.bytes[4] = descriptor->bInterfaceProtocol;
    }
    const uint8_t *raw = (const uint8_t *)descriptor;
    const uint16_t scan_limit = consumed > 0 && consumed <= length ? consumed : length;
    for (uint16_t position = 0; position + 2u <= scan_limit;) {
        const uint8_t size = raw[position];
        if (size < 2 || position + size > scan_limit) break;
        if (raw[position + 1] == TUSB_DESC_ENDPOINT && size >= sizeof(tusb_desc_endpoint_t)) {
            const uint8_t endpoint = raw[position + 2];
            const uint8_t attributes = raw[position + 3] & 3u;
            if ((endpoint & 0x80u) && attributes == TUSB_XFER_INTERRUPT) row.bytes[5] = endpoint;
        }
        position = (uint16_t)(position + size);
    }
    if (consumed) status_endpoint = row.bytes[5];
    row.bytes[6] = (uint8_t)(length >> 8);
    row.bytes[7] = (uint8_t)length;
    row.bytes[8] = (uint8_t)(consumed >> 8);
    row.bytes[9] = (uint8_t)consumed;
    capture(row, consumed == 0);
    return consumed;
}

bool __wrap_hcd_setup_send(uint8_t root, uint8_t address, const uint8_t setup[8])
{
    if (!watched(address)) return __real_hcd_setup_send(root, address, setup);
    if (address == 0) {
        static const uint8_t get_device_header[] = {0x80, TUSB_REQ_GET_DESCRIPTOR, 0,
                                                    TUSB_DESC_DEVICE, 0, 0, 8, 0};
        child_descriptor_stage = memcmp(setup, get_device_header, sizeof(get_device_header)) == 0
                                     ? DESC_SETUP : DESC_IDLE;
    }
    /* Copy borrowed setup bytes before handing them to the HCD. */
    hub_row row = {'Q', address, {root}};
    memcpy(&row.bytes[1], setup, 8);
    hub_row *entry = capture(row, false);
    const bool queued = __real_hcd_setup_send(root, address, setup);
    if (!queued && address == 0) child_descriptor_stage = DESC_IDLE;
    capture_admission(entry, row, 9, queued);
    return queued;
}

bool __wrap_hcd_edpt_xfer(uint8_t root, uint8_t address, uint8_t endpoint, uint8_t *buffer, uint16_t len)
{
    if (address == 0 && watched(0)) {
        if (endpoint == 0x80 && len == 8 && buffer != NULL && child_descriptor_stage == DESC_SETUP) {
            child_descriptor_buffer = buffer;
            child_descriptor_stage = DESC_IN;
            const bool queued = __real_hcd_edpt_xfer(root, address, endpoint, buffer, len);
            if (!queued) child_descriptor_stage = DESC_IDLE;
            return queued;
        }

        hub_row row = {'B', 0, {root}};
        const uint32_t interrupts = save_and_disable_interrupts();
        /* Stock TinyUSB submits status OUT after consuming the matching IN completion. */
        const bool received = endpoint == 0 && len == 0 && child_descriptor_stage == DESC_RECEIVED;
        if (received) memcpy(&row.bytes[2], child_descriptor_buffer, 8);
        child_descriptor_stage = DESC_IDLE;
        restore_interrupts(interrupts);
        if (received) {
            row.bytes[1] = 8;
            capture(row, false);
        }
        return __real_hcd_edpt_xfer(root, address, endpoint, buffer, len);
    }
    if (!watched(address) || address != root_address || endpoint == 0 || endpoint != status_endpoint) {
        return __real_hcd_edpt_xfer(root, address, endpoint, buffer, len);
    }
    const hub_row row = {'I', address, {root, endpoint, (uint8_t)(len >> 8), (uint8_t)len}};
    hub_row *entry = capture(row, false);
    const bool queued = __real_hcd_edpt_xfer(root, address, endpoint, buffer, len);
    capture_admission(entry, row, 4, queued);
    return queued;
}

void __wrap_hcd_event_handler(const hcd_event_t *event, bool in_isr)
{
    if (event->event_id == HCD_EVENT_DEVICE_ATTACH || event->event_id == HCD_EVENT_DEVICE_REMOVE) {
        const bool attach = event->event_id == HCD_EVENT_DEVICE_ATTACH;
        const uint8_t parent = event->connection.hub_addr;
        if (parent != 0 || !attach) child_descriptor_stage = DESC_IDLE;
        if (attach && parent != 0 && parent == root_address) child_attach_seen = true;
        if (!attach && parent == 0) child_attach_seen = false;
        const hub_row row = {attach ? 'A' : 'R', parent,
                             {event->rhport, event->connection.hub_port, in_isr}};
        capture(row, false);
    } else if (event->event_id == HCD_EVENT_XFER_COMPLETE && watched(event->dev_addr)) {
        const uint8_t endpoint = event->xfer_complete.ep_addr;
        if (event->dev_addr == 0) {
            if (child_descriptor_stage == DESC_IN && endpoint == 0x80) {
                child_descriptor_stage = event->xfer_complete.result == XFER_RESULT_SUCCESS &&
                                                 event->xfer_complete.len == 8 ? DESC_RECEIVED : DESC_IDLE;
            } else if (event->xfer_complete.result != XFER_RESULT_SUCCESS) {
                child_descriptor_stage = DESC_IDLE;
            }
        }
        const bool control = (endpoint & 0x7fu) == 0;
        if (control || (event->dev_addr == root_address && endpoint == status_endpoint)) {
            const uint32_t len = event->xfer_complete.len;
            const uint8_t result = event->xfer_complete.result;
            const hub_row row = {'X', event->dev_addr,
                                 {endpoint, result, (uint8_t)(len >> 24), (uint8_t)(len >> 16),
                                  (uint8_t)(len >> 8), (uint8_t)len, in_isr}};
            capture(row, result != XFER_RESULT_SUCCESS);
        }
    }
    __real_hcd_event_handler(event, in_isr);
}

void gridpico_hub_trace_serial_reset(void)
{
    command = 0;
    output = false;
}

void gridpico_hub_trace_serial_start(void)
{
    const uint32_t interrupts = save_and_disable_interrupts();
    snapshot_count = count;
    snapshot_lost = lost;
    snapshot_failed = has_failure;
    if (has_failure) snapshot_failure = failure;
    restore_interrupts(interrupts);
    cursor = 0;
    output = true;
}

bool gridpico_hub_trace_serial_active(void)
{
    return output;
}

void gridpico_hub_trace_serial_byte(uint8_t byte)
{
    static const uint8_t name[] = "HUBTRACE";
    if (byte == '\n') {
        if (command == sizeof(name) - 1) gridpico_hub_trace_serial_start();
        command = 0;
    } else if (command < sizeof(name) - 1 && byte == name[command]) {
        ++command;
    } else {
        command = UINT8_MAX;
    }
}

static bool emit_row(bool (*write)(const char *, uint32_t), const char *prefix, const hub_row *row)
{
    char text[40];
    static const char hex[] = "0123456789ABCDEF";
    const int head = snprintf(text, sizeof(text), "%s %c %02X ", prefix, row->kind, (unsigned)row->address);
    if (head < 0 || (size_t)head + 2 * DATA + 1 > sizeof(text)) return false;
    size_t len = (size_t)head;
    for (unsigned i = 0; i < DATA; ++i) {
        text[len++] = hex[row->bytes[i] >> 4];
        text[len++] = hex[row->bytes[i] & 15u];
    }
    text[len++] = '\n';
    return write(text, (uint32_t)len);
}

void gridpico_hub_trace_serial_pump(bool (*write)(const char *, uint32_t))
{
    if (!output) return;
    if (cursor < snapshot_count) {
        if (emit_row(write, "HUB", &history[cursor])) ++cursor;
        return;
    }
    if (snapshot_failed) {
        if (emit_row(write, "FAIL", &snapshot_failure)) snapshot_failed = false;
        return;
    }
    char text[32];
    const int len = snprintf(text, sizeof(text), "END HUBTRACE %u\n", (unsigned)snapshot_lost);
    if (len > 0 && (size_t)len < sizeof(text) && write(text, (uint32_t)len)) output = false;
}
