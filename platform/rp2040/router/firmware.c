#include "app/rp2040_router.h"
#include "bootsel_line.h"

#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pio_usb.h"
#include "tusb.h"
#include "device/usbd_pvt.h"

#ifdef GRIDPICO_RP2040_BENCH_TRACE
#include <stdio.h>
#endif
#ifdef GRIDPICO_RP2040_PIO_DIAGNOSTICS
#include "hardware/gpio.h"
#include "host/hcd.h"
#endif
#ifdef GRIDPICO_RP2040_HUB_TRACE
#include "hub_trace.h"
#endif

#if !PICO_NO_FLASH || TUSB_VERSION_NUMBER != 2100
#error "Build the initial router as no_flash with TinyUSB 0.21.0"
#endif

_Static_assert(GRIDPICO_APC_VID >= 0 && GRIDPICO_APC_VID <= UINT16_MAX &&
               GRIDPICO_APC_PID >= 0 && GRIDPICO_APC_PID <= UINT16_MAX &&
               GRIDPICO_SYNTH_VID >= 0 && GRIDPICO_SYNTH_VID <= UINT16_MAX &&
               GRIDPICO_SYNTH_PID >= 0 && GRIDPICO_SYNTH_PID <= UINT16_MAX,
               "PIO selectors must be 16-bit USB IDs");
_Static_assert(GRIDPICO_APC_INTERFACE >= 0 && GRIDPICO_APC_INTERFACE <= UINT8_MAX &&
               GRIDPICO_SYNTH_INTERFACE >= 0 && GRIDPICO_SYNTH_INTERFACE <= UINT8_MAX,
               "PIO MIDI interfaces must fit in a byte");

enum {
    DEVICE_ROOT = 0,
    HOST_ROOT = 1,
    HOST_DP = 14, /* DPDM pinout uses the next GPIO for D-. */
    NO_INTERFACE = UINT8_MAX,
    WATCHDOG_MS = 2000,
    NATIVE_WINDOW_US = 1000000,
    BOOTSEL_DRAIN_US = 100000,
    NATIVE_LED_REFRESH_US = 1000000,
    TX_TIMEOUT_US = 250000,
    RX_BATCH = 8,
};

typedef struct {
    bool present;
    bool faulted;
    bool busy;
    uint16_t vid;
    uint16_t pid;
    uint8_t device_address;
    uint8_t number;
    uint8_t rx_cables;
    uint8_t tx_cables;
    uint64_t deadline;
    gridpico_pico_packet in_flight;
} host_interface;

static gridpico_pico_router router;
static gridpico_bootsel_line command;
static bool banner_pending;
static uint64_t native_led_refresh_at;
static host_interface interfaces[CFG_TUH_MIDI];
static uint8_t host_endpoints_out[CFG_TUH_MIDI];
static uint8_t apc_interface = NO_INTERFACE;
static uint8_t synth_interface = NO_INTERFACE;
static bool apc_ambiguous;
static bool synth_ambiguous;

#ifdef GRIDPICO_RP2040_PIO_DIAGNOSTICS
enum { START_ATTEMPTED = 1, CLOCK_SET = 2, HOST_CONFIGURED = 4, HOST_STARTED = 8 };
static uint8_t pio_start_flags;
static uint64_t pio_start_time;
static volatile uint16_t attach_events;
static volatile uint16_t remove_events;
#endif

#ifdef GRIDPICO_RP2040_BENCH_TRACE
enum { TRACE_ROWS = 32 };
typedef struct {
    char kind;
    uint8_t index;
    uint8_t bytes[7];
} trace_row;

static trace_row trace_rows[TRACE_ROWS], trace_snapshot[TRACE_ROWS];
static uint8_t trace_head, trace_count, trace_cursor, trace_remaining, trace_command;

static void fill_row(trace_row *row, char kind, uint8_t index, const uint8_t *bytes, size_t count)
{
    *row = (trace_row){.kind = kind, .index = index};
    for (size_t i = 0; i < count; ++i) row->bytes[i] = bytes[i];
}

static void add_trace(char kind, uint8_t index, const uint8_t *bytes, size_t count)
{
    fill_row(&trace_rows[trace_head], kind, index, bytes, count);
    trace_head = (uint8_t)((trace_head + 1) % TRACE_ROWS);
    if (trace_count < TRACE_ROWS) ++trace_count;
}

#ifdef GRIDPICO_RP2040_PIO_DIAGNOSTICS
enum { EXTRA_SNAPSHOT_ROWS = 3 + CFG_TUH_MIDI };

static uint8_t snapshot_pio(trace_row *rows)
{
    const uint32_t clock = clock_get_hz(clk_sys);
    const uint8_t startup[] = {pio_start_flags, (uint8_t)(clock >> 24), (uint8_t)(clock >> 16),
                               (uint8_t)(clock >> 8), (uint8_t)clock, CFG_TUH_HUB, HOST_DP};
    fill_row(&rows[0], 'P', 0, startup, sizeof(startup));
    const bool started = (pio_start_flags & HOST_STARTED) != 0;
    const uint32_t frame = started ? hcd_frame_number(HOST_ROOT) : 0;
    const uint64_t elapsed = started ? (time_us_64() - pio_start_time) / 1000u : 0;
    const uint16_t age = elapsed > UINT16_MAX ? UINT16_MAX : (uint16_t)elapsed;
    const uint8_t pins = (uint8_t)((gpio_get_all() >> HOST_DP) & 3u);
    const bool connected = started && hcd_port_connect_status(HOST_ROOT);
    const uint8_t line = started ?
        (uint8_t)(pins | (connected << 2) | ((get_core_num() != 0) << 3)) : 0;
    const uint8_t root[] = {(uint8_t)(frame >> 24), (uint8_t)(frame >> 16), (uint8_t)(frame >> 8),
                            (uint8_t)frame, (uint8_t)(age >> 8), (uint8_t)age, line};
    fill_row(&rows[1], 'R', HOST_ROOT, root, sizeof(root));
    const uint16_t attach = attach_events;
    const uint16_t remove = remove_events;
    const uint8_t stack[] = {(uint8_t)(attach >> 8), (uint8_t)attach,
                             (uint8_t)(remove >> 8), (uint8_t)remove};
    fill_row(&rows[2], 'A', HOST_ROOT, stack, sizeof(stack));
    uint8_t count = 3;
    for (uint8_t i = 0; started && i < CFG_TUH_MIDI; ++i) {
        tuh_itf_info_t info;
        if (!tuh_midi_mounted(i) || !tuh_midi_itf_get_info(i, &info)) continue;
        const uint8_t midi[] = {info.daddr, info.desc.bInterfaceNumber, tuh_midi_get_rx_cable_count(i),
                                tuh_midi_get_tx_cable_count(i)};
        fill_row(&rows[count++], 'J', i, midi, sizeof(midi));
    }
    return count;
}
#else
enum { EXTRA_SNAPSHOT_ROWS = 0 };
#endif

static void trace_request(uint8_t byte)
{
    static const uint8_t name[] = "TRACE";
    if (byte == '\n') {
        if (trace_command == sizeof(name) - 1) {
            const gridpico_route_snapshot pad = gridpico_pico_router_pad_status(&router);
            const uint8_t state[] = {pad.enabled, pad.active, apc_interface, synth_interface};
            const unsigned history_limit = TRACE_ROWS - EXTRA_SNAPSHOT_ROWS - 1;
            const unsigned history = trace_count < history_limit ? trace_count : history_limit;
            for (unsigned i = 0; i < history; ++i) {
                trace_snapshot[i] = trace_rows[(trace_head + TRACE_ROWS - history + i) % TRACE_ROWS];
            }
            unsigned count = history;
            fill_row(&trace_snapshot[count++], 'S', 0, state, sizeof(state));
#ifdef GRIDPICO_RP2040_PIO_DIAGNOSTICS
            count += snapshot_pio(&trace_snapshot[count]);
#endif
            trace_cursor = 0;
            trace_remaining = (uint8_t)(count + 1);
        }
        trace_command = 0;
    } else if (trace_command < sizeof(name) - 1 && byte == name[trace_command]) {
        ++trace_command;
    } else {
        trace_command = UINT8_MAX;
    }
}
#else
static void add_trace(char kind, uint8_t index, const uint8_t *bytes, size_t count)
{
    (void)kind;
    (void)index;
    (void)bytes;
    (void)count;
}
#endif

static bool write_cdc(const char *message, uint32_t length)
{
    if (!tud_cdc_connected() || tud_cdc_write_available() < length) return false;
    tud_cdc_write(message, length);
    tud_cdc_write_flush();
    return true;
}

static _Noreturn void request_bootsel(void)
{
    static const char reply[] = "OK BOOTSEL\n";
    const uint64_t deadline = time_us_64() + BOOTSEL_DRAIN_US;
    bool replied = false;
    do {
        if (!replied) replied = write_cdc(reply, sizeof(reply) - 1);
        tud_task_ext(0, false);
    } while (time_us_64() < deadline);
    reset_usb_boot(0, 0);
}

void tud_cdc_line_state_cb(uint8_t index, bool dtr, bool rts)
{
    (void)rts;
    if (index == 0 && gridpico_bootsel_set_dtr(&command, dtr)) {
        tud_cdc_n_read_flush(index);
        banner_pending = dtr;
#ifdef GRIDPICO_RP2040_BENCH_TRACE
        trace_command = trace_remaining = 0;
#endif
#ifdef GRIDPICO_RP2040_HUB_TRACE
        gridpico_hub_trace_serial_reset();
#endif
    }
}

void tud_umount_cb(void)
{
    gridpico_bootsel_set_dtr(&command, false);
    banner_pending = false;
    native_led_refresh_at = 0;
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_WEB, false);
#ifdef GRIDPICO_RP2040_BENCH_TRACE
    trace_command = trace_remaining = 0;
#endif
#ifdef GRIDPICO_RP2040_HUB_TRACE
    gridpico_hub_trace_serial_reset();
#endif
}

static void service_cdc(void)
{
    static const char banner[] = "Gridpico RP2040 router\n";
    if (!tud_cdc_connected()) return;
    if (banner_pending && write_cdc(banner, sizeof(banner) - 1)) banner_pending = false;
    uint8_t bytes[32];
    const uint32_t count = tud_cdc_read(bytes, sizeof(bytes));
    for (uint32_t i = 0; i < count; ++i) {
        if (gridpico_bootsel_feed(&command, bytes[i])) request_bootsel();
#ifdef GRIDPICO_RP2040_BENCH_TRACE
        trace_request(bytes[i]);
#endif
#ifdef GRIDPICO_RP2040_HUB_TRACE
        gridpico_hub_trace_serial_byte(bytes[i]);
#endif
    }
#ifdef GRIDPICO_RP2040_BENCH_TRACE
    if (trace_remaining == 1) {
        if (write_cdc("END TRACE\n", 10)) --trace_remaining;
    } else if (trace_remaining > 1) {
        const trace_row *row = &trace_snapshot[trace_cursor];
        const uint8_t *d = row->bytes;
        char output[32];
        const int length = snprintf(output, sizeof(output), "%c %02X %02X%02X%02X%02X%02X%02X%02X\n",
                                    row->kind, (unsigned)row->index, (unsigned)d[0], (unsigned)d[1],
                                    (unsigned)d[2], (unsigned)d[3], (unsigned)d[4], (unsigned)d[5],
                                    (unsigned)d[6]);
        if (length > 0 && (size_t)length < sizeof(output) && write_cdc(output, (uint32_t)length)) {
            ++trace_cursor;
            --trace_remaining;
        }
    }
#endif
#ifdef GRIDPICO_RP2040_HUB_TRACE
    if (trace_remaining == 0) gridpico_hub_trace_serial_pump(write_cdc);
#endif
}

static uint8_t select_interface(uint16_t vid, uint16_t pid, uint8_t number, uint8_t cables, bool *ambiguous)
{
    if (vid == 0 || pid == 0 || number == NO_INTERFACE) return NO_INTERFACE;
    uint8_t selected = NO_INTERFACE;
    for (uint8_t i = 0; i < CFG_TUH_MIDI; ++i) {
        const host_interface *itf = &interfaces[i];
        if (itf->present && itf->vid == vid && itf->pid == pid && itf->number == number &&
            itf->rx_cables == cables && itf->tx_cables == cables) {
            if (selected != NO_INTERFACE) {
                *ambiguous = true;
                return NO_INTERFACE;
            }
            selected = i;
        }
    }
    if (selected == NO_INTERFACE) *ambiguous = false;
    return selected != NO_INTERFACE && !*ambiguous && !interfaces[selected].faulted ? selected : NO_INTERFACE;
}

static void replace_attachment(gridpico_pico_peer peer, uint8_t *current, uint8_t next)
{
    if (*current == next) return;
    if (*current != NO_INTERFACE && interfaces[*current].busy && host_endpoints_out[*current] != 0) {
        tuh_edpt_abort_xfer(interfaces[*current].device_address, host_endpoints_out[*current]);
    }
    gridpico_pico_router_attach(&router, peer, false);
    *current = next;
    gridpico_pico_router_attach(&router, peer, next != NO_INTERFACE);
}

static void reconcile_host(void)
{
    replace_attachment(GRIDPICO_PICO_APC, &apc_interface,
                       select_interface(GRIDPICO_APC_VID, GRIDPICO_APC_PID, GRIDPICO_APC_INTERFACE,
                                        2, &apc_ambiguous));
    replace_attachment(GRIDPICO_PICO_SYNTH, &synth_interface,
                       select_interface(GRIDPICO_SYNTH_VID, GRIDPICO_SYNTH_PID, GRIDPICO_SYNTH_INTERFACE,
                                        1, &synth_ambiguous));
}

#ifdef GRIDPICO_RP2040_PIO_DIAGNOSTICS
void tuh_event_hook_cb(uint8_t root, uint32_t event, bool in_isr)
{
    (void)in_isr;
    if (root != HOST_ROOT) return;
    if (event == HCD_EVENT_DEVICE_ATTACH && attach_events != UINT16_MAX) ++attach_events;
    if (event == HCD_EVENT_DEVICE_REMOVE && remove_events != UINT16_MAX) ++remove_events;
}

void tuh_enum_descriptor_device_cb(uint8_t address, const tusb_desc_device_t *descriptor)
{
    tuh_bus_info_t bus;
    tuh_bus_info_get(address, &bus);
    const uint16_t vid = tu_le16toh(descriptor->idVendor);
    const uint16_t pid = tu_le16toh(descriptor->idProduct);
    const uint8_t values[] = {(uint8_t)(vid >> 8), (uint8_t)vid, (uint8_t)(pid >> 8), (uint8_t)pid,
                              bus.hub_addr, bus.hub_port, descriptor->bDeviceClass};
    add_trace('D', address, values, sizeof(values));
#ifdef GRIDPICO_RP2040_HUB_TRACE
    if (bus.hub_addr == 0 && descriptor->bDeviceClass == TUSB_CLASS_HUB) {
        gridpico_hub_trace_device(address, descriptor->bDeviceSubClass, descriptor->bDeviceProtocol);
    }
#endif
}

bool tuh_enum_descriptor_configuration_cb(uint8_t address, uint8_t index,
                                          const tusb_desc_configuration_t *descriptor)
{
    const uint16_t length = tu_le16toh(descriptor->wTotalLength);
    const uint8_t values[] = {index, descriptor->bNumInterfaces, (uint8_t)(length >> 8), (uint8_t)length};
    add_trace('C', address, values, sizeof(values));
#ifdef GRIDPICO_RP2040_HUB_TRACE
    gridpico_hub_trace_config(address);
#endif
    return true;
}

void tuh_mount_cb(uint8_t address)
{
    tuh_bus_info_t bus;
    tuh_bus_info_get(address, &bus);
    const uint8_t values[] = {bus.hub_addr, bus.hub_port};
    add_trace('N', address, values, sizeof(values));
}
#endif

void tuh_midi_descriptor_cb(uint8_t index, const tuh_midi_descriptor_cb_t *descriptor)
{
    if (index < CFG_TUH_MIDI && descriptor->desc_epout != NULL) {
        host_endpoints_out[index] = descriptor->desc_epout->bEndpointAddress;
    }
#ifdef GRIDPICO_RP2040_PIO_DIAGNOSTICS
    const uint8_t values[] = {
        descriptor->desc_midi == NULL ? UINT8_MAX : descriptor->desc_midi->bInterfaceNumber,
        descriptor->desc_epin == NULL ? 0 : descriptor->desc_epin->bEndpointAddress,
        descriptor->desc_epout == NULL ? 0 : descriptor->desc_epout->bEndpointAddress,
    };
    add_trace('K', index, values, sizeof(values));
#endif
}

void tuh_midi_umount_cb(uint8_t index)
{
    if (index >= CFG_TUH_MIDI) return;
    interfaces[index] = (host_interface){0};
    host_endpoints_out[index] = 0;
    reconcile_host();
}

void tuh_midi_mount_cb(uint8_t index, const tuh_midi_mount_cb_t *mount)
{
#ifdef GRIDPICO_RP2040_PIO_DIAGNOSTICS
    const uint8_t values[] = {mount->daddr, mount->bInterfaceNumber, mount->rx_cable_count,
                              mount->tx_cable_count};
    add_trace('V', index, values, sizeof(values));
#endif
    if (index >= CFG_TUH_MIDI) return;
    if (interfaces[index].present) return;
    uint16_t vid, pid;
    if (!tuh_vid_pid_get(mount->daddr, &vid, &pid)) {
#ifdef GRIDPICO_RP2040_PIO_DIAGNOSTICS
        add_trace('E', index, values, sizeof(values));
#endif
        return;
    }
    interfaces[index] = (host_interface){
        .present = true, .vid = vid, .pid = pid,
        .device_address = mount->daddr, .number = mount->bInterfaceNumber,
        .rx_cables = mount->rx_cable_count, .tx_cables = mount->tx_cable_count,
    };
    const uint8_t identity[] = {(uint8_t)(vid >> 8), (uint8_t)vid, (uint8_t)(pid >> 8), (uint8_t)pid,
                                mount->bInterfaceNumber, mount->rx_cable_count, mount->tx_cable_count};
    add_trace('M', index, identity, sizeof(identity));
    reconcile_host();
}

#ifdef GRIDPICO_RP2040_HCD_TRACE
bool __real_midih_xfer_cb(uint8_t address, uint8_t endpoint, xfer_result_t result, uint32_t transferred);

bool __wrap_midih_xfer_cb(uint8_t address, uint8_t endpoint, xfer_result_t result, uint32_t transferred)
{
    if (address != 0 && endpoint != 0 && tu_edpt_dir(endpoint) == TUSB_DIR_OUT) {
        uint8_t matched = NO_INTERFACE;
        for (uint8_t i = 0; i < CFG_TUH_MIDI; ++i) {
            const host_interface *itf = &interfaces[i];
            if ((i == apc_interface || i == synth_interface) && itf->present && itf->busy &&
                !itf->faulted && itf->device_address == address && host_endpoints_out[i] == endpoint) {
                if (matched != NO_INTERFACE) {
                    matched = NO_INTERFACE;
                    break;
                }
                matched = i;
            }
        }
        if (matched != NO_INTERFACE) {
            const uint8_t values[] = {address, endpoint, (uint8_t)result, (uint8_t)(transferred >> 24),
                                      (uint8_t)(transferred >> 16), (uint8_t)(transferred >> 8),
                                      (uint8_t)transferred};
            add_trace('H', matched, values, sizeof(values));
        }
    }
    return __real_midih_xfer_cb(address, endpoint, result, transferred);
}
#endif

void tuh_midi_tx_cb(uint8_t index, uint32_t transferred)
{
    if (index >= CFG_TUH_MIDI || !interfaces[index].busy) return;
    host_interface *itf = &interfaces[index];
    itf->busy = false;
    const bool complete = transferred == GRIDPICO_PICO_PACKET_BYTES;
    add_trace(complete ? 'T' : 'F', index, itf->in_flight.bytes, sizeof(itf->in_flight.bytes));
    if (!complete) {
        itf->faulted = true;
        reconcile_host();
    }
}

static bool submit_host(uint8_t index, const gridpico_pico_packet *packet)
{
    if (index == NO_INTERFACE || packet == NULL || interfaces[index].busy) return false;
    if (!tuh_midi_packet_write(index, packet->bytes)) return false;
    host_interface *itf = &interfaces[index];
    itf->in_flight = *packet;
    itf->busy = true;
    itf->deadline = time_us_64() + TX_TIMEOUT_US;
    tuh_midi_write_flush(index);
    return true;
}

static void receive(gridpico_pico_peer peer, const gridpico_pico_packet *packet)
{
    add_trace('I', (uint8_t)peer, packet->bytes, sizeof(packet->bytes));
    gridpico_pico_router_receive(&router, peer, packet);
}

static void service_host(void)
{
    for (uint8_t index = 0; index < CFG_TUH_MIDI; ++index) {
        host_interface *itf = &interfaces[index];
        if (!itf->present || !tuh_midi_mounted(index)) continue;
        if (itf->busy && !itf->faulted && time_us_64() >= itf->deadline) {
            itf->faulted = true;
            add_trace('F', index, itf->in_flight.bytes, sizeof(itf->in_flight.bytes));
            reconcile_host();
        }
        gridpico_pico_packet packet;
        for (unsigned i = 0; i < RX_BATCH && tuh_midi_packet_read(index, packet.bytes); ++i) {
            if (index == apc_interface) receive(GRIDPICO_PICO_APC, &packet);
        }
    }
    const gridpico_pico_packet *note = gridpico_pico_router_note(&router, GRIDPICO_PICO_SYNTH);
    if (submit_host(synth_interface, note)) {
        add_trace('G', GRIDPICO_PICO_SYNTH, note->bytes, sizeof(note->bytes));
        gridpico_pico_router_note_sent(&router, GRIDPICO_PICO_SYNTH);
    }
    if (submit_host(apc_interface, gridpico_pico_router_led(&router, GRIDPICO_PICO_APC))) {
        gridpico_pico_router_led_sent(&router, GRIDPICO_PICO_APC);
    }
}

static void service_native_midi(void)
{
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_WEB, tud_mounted());
    if (!router.attached[GRIDPICO_PICO_WEB]) return;
    gridpico_pico_packet packet;
    for (unsigned i = 0; i < RX_BATCH && tud_midi_packet_read(packet.bytes); ++i) {
        receive(GRIDPICO_PICO_WEB, &packet);
    }
    const gridpico_pico_packet *note = gridpico_pico_router_note(&router, GRIDPICO_PICO_WEB);
    if (note != NULL && tud_midi_packet_write(note->bytes)) {
        add_trace('G', GRIDPICO_PICO_WEB, note->bytes, sizeof(note->bytes));
        gridpico_pico_router_note_sent(&router, GRIDPICO_PICO_WEB);
    }
    const gridpico_pico_packet *led = gridpico_pico_router_led(&router, GRIDPICO_PICO_WEB);
    if (led != NULL && tud_midi_packet_write(led->bytes)) gridpico_pico_router_led_sent(&router, GRIDPICO_PICO_WEB);

    /* USB configuration does not indicate that a native MIDI reader is listening. */
    const uint64_t now = time_us_64();
    if (native_led_refresh_at == 0) {
        native_led_refresh_at = now + NATIVE_LED_REFRESH_US;
    } else if (now >= native_led_refresh_at) {
        native_led_refresh_at = now + NATIVE_LED_REFRESH_US;
        if (note == NULL && led == NULL && tud_midi_mounted() && !tud_suspended() &&
            usbd_edpt_ready(DEVICE_ROOT, GRIDPICO_NATIVE_MIDI_IN_EP)) {
            tud_midi_packet_write(router.led.bytes);
        }
    }
}

static bool start_host(void)
{
#ifdef GRIDPICO_RP2040_PIO_DIAGNOSTICS
    pio_start_flags |= START_ATTEMPTED;
#endif
    if (!set_sys_clock_khz(120000, false)) return false;
#ifdef GRIDPICO_RP2040_PIO_DIAGNOSTICS
    pio_start_flags |= CLOCK_SET;
#endif
    pio_usb_configuration_t pio = PIO_USB_DEFAULT_CONFIG;
    pio.pin_dp = HOST_DP;
    const tusb_rhport_init_t host = {.role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_FULL};
    if (!tuh_configure(HOST_ROOT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio)) return false;
#ifdef GRIDPICO_RP2040_PIO_DIAGNOSTICS
    pio_start_flags |= HOST_CONFIGURED;
#endif
    if (!tusb_init(HOST_ROOT, &host)) return false;
#ifdef GRIDPICO_RP2040_PIO_DIAGNOSTICS
    pio_start_time = time_us_64();
    pio_start_flags |= HOST_STARTED;
#endif
    return true;
}

int main(void)
{
    watchdog_enable(WATCHDOG_MS, false);
    const tusb_rhport_init_t device = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    if (!tusb_init(DEVICE_ROOT, &device) || gridpico_pico_router_init(&router) != GRIDPICO_OK) {
        reset_usb_boot(0, 0);
    }

    const uint64_t native_first_until = time_us_64() + NATIVE_WINDOW_US;
    bool host_attempted = false;
    bool host_ready = false;
    for (;;) {
        tud_task_ext(0, false);
        service_cdc();
        if (!host_attempted && time_us_64() >= native_first_until) {
            host_attempted = true;
            watchdog_update();
            host_ready = start_host();
        }
        if (host_ready) {
            tuh_task_ext(0, false);
            service_host();
        }
        service_native_midi();
        watchdog_update();
    }
}
