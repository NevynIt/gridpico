#include "app/rp2040_router.h"
#include "bootsel_line.h"

#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pio_usb.h"
#include "tusb.h"
#include "device/usbd_pvt.h"

#ifdef GRIDPICO_RP2040_FLASH_ROUTER
#include "flash_startup.h"
#include "hardware/structs/watchdog.h"
#include "device/dcd.h"
#endif
#if defined(GRIDPICO_RP2040_FLASH_ROUTER) || defined(GRIDPICO_RP2040_PATCHBAY_BENCH)
#include "hardware/sync.h"
#endif
#ifdef GRIDPICO_RP2040_BENCH_TRACE
#include <stdio.h>
#endif
#ifdef GRIDPICO_RP2040_PIO_DIAGNOSTICS
#include "hardware/gpio.h"
#endif
#if defined(GRIDPICO_RP2040_PIO_DIAGNOSTICS) || defined(GRIDPICO_RP2040_FLASH_ROUTER)
#include "host/hcd.h"
#endif
#ifdef GRIDPICO_RP2040_HUB_TRACE
#include "hub_trace.h"
#endif
#ifdef GRIDPICO_RP2040_ONBOARD_STATUS_BENCH
#include "onboard_status.h"
#endif
#ifdef GRIDPICO_RP2040_MATRIX_CLI
#include "cdc_cli.h"
#endif

#if TUSB_VERSION_NUMBER != 2100
#error "Build the router with TinyUSB 0.21.0"
#endif
#if defined(GRIDPICO_RP2040_FLASH_ROUTER)
#if PICO_NO_FLASH || !PICO_COPY_TO_RAM
#error "Build the flash router as copy_to_ram"
#endif
#elif !PICO_NO_FLASH
#error "The default router build is no_flash"
#endif

_Static_assert(GRIDPICO_APC_VID >= 0 && GRIDPICO_APC_VID <= UINT16_MAX &&
               GRIDPICO_APC_PID >= 0 && GRIDPICO_APC_PID <= UINT16_MAX &&
               GRIDPICO_SYNTH_VID >= 0 && GRIDPICO_SYNTH_VID <= UINT16_MAX &&
               GRIDPICO_SYNTH_PID >= 0 && GRIDPICO_SYNTH_PID <= UINT16_MAX,
               "PIO selectors must be 16-bit USB IDs");
_Static_assert(GRIDPICO_APC_INTERFACE >= 0 && GRIDPICO_APC_INTERFACE <= UINT8_MAX &&
               GRIDPICO_SYNTH_INTERFACE >= 0 && GRIDPICO_SYNTH_INTERFACE <= UINT8_MAX,
               "PIO MIDI interfaces must fit in a byte");

#ifdef GRIDPICO_RP2040_PROGRAM_CHANGE_BENCH
_Static_assert(GRIDPICO_SYNTH_VID == 0x1c75 && GRIDPICO_SYNTH_PID == 0x0601 &&
               GRIDPICO_SYNTH_INTERFACE == 2, "Program Change requires the selected MicroFreak");
#endif

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
#ifdef GRIDPICO_RP2040_MATRIX_CLI
enum { CDC_OUTPUT_BYTES_PER_PASS = 64 };
static uint32_t cdc_output_budget;
static gridpico_cdc_cli cdc_cli;
static gridpico_cli_frontend cli;
static gridpico_local_routing_snapshot routing_scratch;
#endif
static uint64_t native_led_refresh_at;
static host_interface interfaces[CFG_TUH_MIDI];
static uint8_t host_endpoints_out[CFG_TUH_MIDI];
static uint8_t apc_interface = NO_INTERFACE;
static uint8_t synth_interface = NO_INTERFACE;
static bool apc_ambiguous;
static bool synth_ambiguous;
#if defined(GRIDPICO_RP2040_PATCHBAY_BENCH) && !defined(GRIDPICO_RP2040_MATRIX_CLI)
enum { CIRCUIT_TRACE_PEER = GRIDPICO_PICO_PEERS };
static uint8_t circuit_interface = NO_INTERFACE;
static bool circuit_ambiguous;
#endif
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
static uint32_t apc_led_generation;
static uint32_t native_led_generation;
#ifdef GRIDPICO_RP2040_KEYS_BENCH
static uint32_t native_note_generation;
static uint64_t native_note_deadline;
static uint64_t native_led_deadline;
static bool native_note_fault;
#endif

extern uint32_t __StackBottom, __StackTop;
static uintptr_t stack_canary_end;
static const uint32_t stack_canary = UINT32_C(0x6ec095ba);

static void mark_unused_stack(void)
{
    const uint32_t interrupts = save_and_disable_interrupts();
    __asm__ volatile ("mov %0, sp" : "=r" (stack_canary_end));
    for (uintptr_t address = (uintptr_t)&__StackBottom; address < stack_canary_end; address += sizeof(uint32_t)) {
        *(volatile uint32_t *)address = stack_canary;
    }
    restore_interrupts(interrupts);
}

static uint16_t untouched_stack(void)
{
    const uintptr_t bottom = (uintptr_t)&__StackBottom;
    uintptr_t address = bottom;
    while (address < stack_canary_end && *(volatile const uint32_t *)address == stack_canary) {
        address += sizeof(uint32_t);
    }
    return (uint16_t)(address - bottom);
}
#endif

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
static uint8_t trace_head, trace_count, trace_cursor, trace_remaining;
#ifndef GRIDPICO_RP2040_MATRIX_CLI
static uint8_t trace_command;
#endif

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
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
enum { EXTRA_SNAPSHOT_ROWS = 4 + CFG_TUH_MIDI };
#else
enum { EXTRA_SNAPSHOT_ROWS = 3 + CFG_TUH_MIDI };
#endif

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
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    const uint16_t untouched = untouched_stack();
    const uint16_t marked = (uint16_t)(stack_canary_end - (uintptr_t)&__StackBottom);
    const uint16_t stack_size = (uint16_t)((uintptr_t)&__StackTop - (uintptr_t)&__StackBottom);
    const uint8_t watermark[] = {(uint8_t)(untouched >> 8), (uint8_t)untouched,
                                 (uint8_t)(marked >> 8), (uint8_t)marked,
                                 (uint8_t)(stack_size >> 8), (uint8_t)stack_size};
    fill_row(&rows[count++], 'W', 0, watermark, sizeof(watermark));
#endif
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

static void start_trace(void)
{
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

#ifndef GRIDPICO_RP2040_MATRIX_CLI
static void trace_request(uint8_t byte)
{
    static const uint8_t name[] = "TRACE";
    if (byte == '\n') {
        if (trace_command == sizeof(name) - 1) start_trace();
        trace_command = 0;
    } else if (trace_command < sizeof(name) - 1 && byte == name[trace_command]) {
        ++trace_command;
    } else {
        trace_command = UINT8_MAX;
    }
}
#endif
#else
static void add_trace(char kind, uint8_t index, const uint8_t *bytes, size_t count)
{
    (void)kind;
    (void)index;
    (void)bytes;
    (void)count;
}
#endif

#ifdef GRIDPICO_RP2040_MATRIX_CLI
static bool trace_active(void)
{
#ifdef GRIDPICO_RP2040_BENCH_TRACE
    if (trace_remaining != 0) return true;
#endif
#ifdef GRIDPICO_RP2040_HUB_TRACE
    return gridpico_hub_trace_serial_active();
#else
    return false;
#endif
}

static gridpico_result start_cli_platform(void *context, unsigned requested)
{
    (void)context;
    if (trace_active()) return GRIDPICO_UNAVAILABLE;
#ifdef GRIDPICO_RP2040_BENCH_TRACE
    if (requested == GRIDPICO_CLI_PLATFORM_TRACE) {
        start_trace();
        return GRIDPICO_OK;
    }
#endif
#ifdef GRIDPICO_RP2040_HUB_TRACE
    if (requested == GRIDPICO_CLI_PLATFORM_HUBTRACE) {
        gridpico_hub_trace_serial_start();
        return GRIDPICO_OK;
    }
#endif
    (void)requested;
    return GRIDPICO_UNAVAILABLE;
}

static void bind_matrix_cli(void)
{
    unsigned capabilities = GRIDPICO_CLI_PLATFORM_BOOTSEL;
#ifdef GRIDPICO_RP2040_BENCH_TRACE
    capabilities |= GRIDPICO_CLI_PLATFORM_TRACE;
#endif
#ifdef GRIDPICO_RP2040_HUB_TRACE
    capabilities |= GRIDPICO_CLI_PLATFORM_HUBTRACE;
#endif
    cli.session_context = &router.session;
    cli.read_snapshot = gridpico_patch_session_read_snapshot;
    /* CLI reads and panel refreshes share the serialized foreground. */
    gridpico_cli_bind_apc_patch(&cli, gridpico_patch_session_read_apc_patch, &router.panel_scratch);
    gridpico_cli_bind_routing(&cli, &router, gridpico_pico_router_read_routing,
                             gridpico_pico_router_read_managed_route, gridpico_pico_router_set_managed_route,
                             &routing_scratch);
    gridpico_cli_bind_platform(&cli, capabilities, start_cli_platform, NULL);
}

static uint32_t send_cdc(const char *message, uint32_t length)
{
    const uint32_t sent = tud_cdc_write(message, length);
    cdc_output_budget -= sent;
    tud_cdc_write_flush();
    return sent;
}
#endif

static bool write_cdc(const char *message, uint32_t length)
{
    if (!tud_cdc_connected() || tud_cdc_write_available() < length) return false;
#ifdef GRIDPICO_RP2040_MATRIX_CLI
    if (length > cdc_output_budget) return false;
    /* Each trace/banner record fits the checked FIFO space. This is the only
       foreground writer, and TinyUSB does not overwrite while DTR is set. */
    return send_cdc(message, length) == length;
#else
    tud_cdc_write(message, length);
    tud_cdc_write_flush();
    return true;
#endif
}

#ifdef GRIDPICO_RP2040_FLASH_ROUTER
enum { FLASH_START_DEADLINE_US = 4000000 };
static volatile bool native_bus_activity;

void tud_event_hook_cb(uint8_t root, uint32_t event, bool in_isr)
{
    (void)in_isr;
    if (root == DEVICE_ROOT && (event == DCD_EVENT_BUS_RESET || event == DCD_EVENT_SETUP_RECEIVED)) {
        native_bus_activity = true;
    }
}

static _Noreturn void enter_rom(void)
{
    gridpico_flash_start_clear(watchdog_hw->scratch);
    watchdog_disable();
    reset_usb_boot(0, 0);
}

static bool check_flash_start(uint64_t deadline, bool host_serviced, bool cdc_ready)
{
    /* Serialize declaring standalone readiness with native bus activity from the DCD IRQ. */
    const uint32_t interrupts = save_and_disable_interrupts();
    const gridpico_flash_start_state state = gridpico_flash_start_poll(
        time_us_64(), deadline, host_serviced, native_bus_activity, cdc_ready);
    if (state == GRIDPICO_FLASH_START_READY) gridpico_flash_start_clear(watchdog_hw->scratch);
    restore_interrupts(interrupts);
    if (state == GRIDPICO_FLASH_START_EXPIRED) enter_rom();
    return state == GRIDPICO_FLASH_START_READY;
}

#ifdef GRIDPICO_RP2040_FLASH_STARTUP_FAULT
static _Noreturn void show_startup_fault(uint64_t deadline)
{
    uint64_t stop_at = deadline;
    for (;;) {
        tud_task_ext(0, false);
        const uint64_t now = time_us_64();
        if (stop_at == deadline && tud_cdc_n_ready(0) && now + 250000u < deadline) {
            stop_at = now + 250000u;
        }
        if (now >= stop_at) {
            for (;;) tight_loop_contents();
        }
        watchdog_update();
    }
}
#endif
#else
#define enter_rom() reset_usb_boot(0, 0)
#endif

static _Noreturn void request_bootsel(void)
{
    static const char reply[] = "OK BOOTSEL\n";
    const uint64_t deadline = time_us_64() + BOOTSEL_DRAIN_US;
#ifdef GRIDPICO_RP2040_ONBOARD_STATUS_BENCH
    gridpico_onboard_show(GRIDPICO_ONBOARD_BOOTSEL);
#endif
#ifdef GRIDPICO_RP2040_MATRIX_CLI
    /* Retire queued CLI bytes. A USB packet already in flight may still finish. */
    tud_cdc_write_clear();
    cdc_output_budget = sizeof(reply) - 1;
#endif
    bool replied = false;
    do {
        if (!replied) replied = write_cdc(reply, sizeof(reply) - 1);
        tud_task_ext(0, false);
    } while (time_us_64() < deadline);
    enter_rom();
}

static void reset_cdc_requests(void)
{
#ifdef GRIDPICO_RP2040_MATRIX_CLI
    gridpico_cdc_cli_reset(&cdc_cli);
#endif
#ifdef GRIDPICO_RP2040_BENCH_TRACE
    trace_remaining = 0;
#ifndef GRIDPICO_RP2040_MATRIX_CLI
    trace_command = 0;
#endif
#endif
#ifdef GRIDPICO_RP2040_HUB_TRACE
    gridpico_hub_trace_serial_reset();
#endif
}

void tud_cdc_line_state_cb(uint8_t index, bool dtr, bool rts)
{
    (void)rts;
    if (index == 0 && gridpico_bootsel_set_dtr(&command, dtr)) {
        tud_cdc_n_read_flush(index);
        banner_pending = dtr;
#ifdef GRIDPICO_RP2040_MATRIX_CLI
        tud_cdc_n_write_clear(index);
#endif
        reset_cdc_requests();
    }
}

void tud_umount_cb(void)
{
    gridpico_bootsel_set_dtr(&command, false);
    banner_pending = false;
    native_led_refresh_at = 0;
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    native_led_generation = 0;
#ifdef GRIDPICO_RP2040_KEYS_BENCH
    native_note_generation = 0;
    native_note_fault = false;
#endif
#endif
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_WEB, false);
    reset_cdc_requests();
}

#ifdef GRIDPICO_RP2040_MATRIX_CLI
__attribute__((noinline))
#endif
static void pump_cdc_trace(void)
{
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

#ifdef GRIDPICO_RP2040_MATRIX_CLI
/* Keep terminal framing and rendering off the musical foreground stack frame. */
__attribute__((noinline))
#endif
static void service_cdc(void)
{
    static const char banner[] = "Gridpico RP2040 router\n";
    if (!tud_cdc_connected()) return;
#ifdef GRIDPICO_RP2040_MATRIX_CLI
    cdc_output_budget = CDC_OUTPUT_BYTES_PER_PASS;
#else
    if (banner_pending && write_cdc(banner, sizeof(banner) - 1)) banner_pending = false;
#endif
    uint8_t bytes[32];
    const uint32_t received = tud_cdc_read(bytes, sizeof(bytes));
    for (uint32_t i = 0; i < received; ++i) {
        if (gridpico_bootsel_feed(&command, bytes[i])) request_bootsel();
#ifdef GRIDPICO_RP2040_MATRIX_CLI
        gridpico_cdc_cli_feed(&cdc_cli, &cli, bytes[i], bytes[i] == '\n' && trace_active());
#else
#ifdef GRIDPICO_RP2040_BENCH_TRACE
        trace_request(bytes[i]);
#endif
#ifdef GRIDPICO_RP2040_HUB_TRACE
        gridpico_hub_trace_serial_byte(bytes[i]);
#endif
#endif
    }
#ifdef GRIDPICO_RP2040_MATRIX_CLI
    if (banner_pending) {
        if (!write_cdc(banner, sizeof(banner) - 1)) return;
        banner_pending = false;
    }
    if (!trace_active()) {
        const char *reply;
        uint32_t length = (uint32_t)gridpico_cdc_cli_offer(&cdc_cli, &reply);
        const uint32_t available = tud_cdc_write_available();
        if (length > available) length = available;
        if (length > cdc_output_budget) length = cdc_output_budget;
        if (length != 0) gridpico_cdc_cli_sent(&cdc_cli, send_cdc(reply, length));
        return;
    }
#endif
    pump_cdc_trace();
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
#if defined(GRIDPICO_RP2040_PATCHBAY_BENCH) && !defined(GRIDPICO_RP2040_MATRIX_CLI)
    circuit_interface = select_interface(0x1235, 0x0139, 1, 1, &circuit_ambiguous);
#endif
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
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    bool apc_led = false;
#endif
#ifdef GRIDPICO_RP2040_KEYS_BENCH
    uint8_t failed_interface = NO_INTERFACE;
#endif
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
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
            apc_led = matched == apc_interface && apc_led_generation == router.generation[GRIDPICO_PICO_APC];
#endif
#ifdef GRIDPICO_RP2040_KEYS_BENCH
            if (result != XFER_RESULT_SUCCESS || transferred != GRIDPICO_PICO_PACKET_BYTES) failed_interface = matched;
#endif
        }
    }
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    const bool handled = __real_midih_xfer_cb(address, endpoint, result, transferred);
    if (apc_led) {
        apc_led_generation = 0;
        gridpico_pico_router_led_complete(&router, GRIDPICO_PICO_APC,
                                           handled && result == XFER_RESULT_SUCCESS && transferred == 4);
    }
#ifdef GRIDPICO_RP2040_KEYS_BENCH
    if (failed_interface != NO_INTERFACE && interfaces[failed_interface].present &&
        interfaces[failed_interface].device_address == address) {
        interfaces[failed_interface].faulted = true;
        reconcile_host();
    }
#endif
    return handled;
#else
    return __real_midih_xfer_cb(address, endpoint, result, transferred);
#endif
}
#endif

#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
bool __real_midid_xfer_cb(uint8_t root, uint8_t endpoint, xfer_result_t result, uint32_t transferred);

bool __wrap_midid_xfer_cb(uint8_t root, uint8_t endpoint, xfer_result_t result, uint32_t transferred)
{
    const bool handled = __real_midid_xfer_cb(root, endpoint, result, transferred);
#ifdef GRIDPICO_RP2040_KEYS_BENCH
    if (root == DEVICE_ROOT && endpoint == GRIDPICO_NATIVE_MIDI_IN_EP && native_note_generation != 0) {
        if (native_note_generation == router.generation[GRIDPICO_PICO_WEB]) {
            native_note_fault |= !handled || result != XFER_RESULT_SUCCESS || transferred != GRIDPICO_PICO_PACKET_BYTES;
        }
        native_note_generation = 0;
    }
#endif
    if (root == DEVICE_ROOT && endpoint == GRIDPICO_NATIVE_MIDI_IN_EP && native_led_generation != 0) {
        if (native_led_generation == router.generation[GRIDPICO_PICO_WEB]) {
            gridpico_pico_router_led_complete(&router, GRIDPICO_PICO_WEB,
                                               handled && result == XFER_RESULT_SUCCESS && transferred == 4);
        }
        native_led_generation = 0;
    }
    return handled;
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
    host_interface *itf = &interfaces[index];
#ifdef GRIDPICO_RP2040_PROGRAM_CHANGE_BENCH
    const bool program_change = packet->bytes[0] == 0x0c;
    if (program_change) {
        const uint8_t endpoint = host_endpoints_out[index];
        if (index != synth_interface || !itf->present || itf->faulted || !tuh_midi_mounted(index) ||
            itf->device_address == 0 || endpoint == 0 || tu_edpt_dir(endpoint) != TUSB_DIR_OUT) {
            return false;
        }
        /* Claim OUT directly so refused admission cannot leave a Program Change in the class FIFO.
           The bound MIDI class still owns completion. */
        itf->in_flight = *packet;
        tuh_xfer_t transfer = {
            .daddr = itf->device_address,
            .ep_addr = endpoint,
            .buflen = sizeof(itf->in_flight.bytes),
            .buffer = itf->in_flight.bytes,
        };
        if (!tuh_edpt_xfer(&transfer)) return false;
    } else
#endif
    {
        if (!tuh_midi_packet_write(index, packet->bytes)) return false;
        itf->in_flight = *packet;
    }
    itf->busy = true;
    itf->deadline = time_us_64() + TX_TIMEOUT_US;
#ifdef GRIDPICO_RP2040_PROGRAM_CHANGE_BENCH
    if (program_change) return true;
#endif
    tuh_midi_write_flush(index);
    return true;
}

static void receive(gridpico_pico_peer peer, const gridpico_pico_packet *packet)
{
    add_trace('I', (uint8_t)peer, packet->bytes, sizeof(packet->bytes));
#ifdef GRIDPICO_RP2040_PROGRAM_CHANGE_BENCH
    if (gridpico_pico_router_can_program_change(&router, peer, packet)) {
        if (submit_host(synth_interface, packet)) {
            add_trace('G', GRIDPICO_PICO_SYNTH, packet->bytes, sizeof(packet->bytes));
        }
        return;
    }
#endif
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
#if defined(GRIDPICO_RP2040_PATCHBAY_BENCH) && !defined(GRIDPICO_RP2040_MATRIX_CLI)
            else if (index == circuit_interface && (packet.bytes[0] == 8 || packet.bytes[0] == 9) &&
                     (packet.bytes[1] >> 4) == packet.bytes[0] &&
                     packet.bytes[2] < 128 && packet.bytes[3] < 128) {
                /* Evidence from the actual named input. No route or output ownership yet. */
                add_trace('I', CIRCUIT_TRACE_PEER, packet.bytes, sizeof(packet.bytes));
            }
#endif
        }
    }
    const gridpico_pico_packet *note = gridpico_pico_router_note(&router, GRIDPICO_PICO_SYNTH);
    if (submit_host(synth_interface, note)) {
        add_trace('G', GRIDPICO_PICO_SYNTH, note->bytes, sizeof(note->bytes));
        gridpico_pico_router_note_sent(&router, GRIDPICO_PICO_SYNTH);
    }
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    if (gridpico_pico_router_note(&router, GRIDPICO_PICO_SYNTH) != NULL) return;
#endif
#ifdef GRIDPICO_RP2040_KEYS_BENCH
    const gridpico_pico_packet *panel = gridpico_pico_router_note(&router, GRIDPICO_PICO_APC);
    if (panel != NULL) {
        if (submit_host(apc_interface, panel)) {
            add_trace('G', GRIDPICO_PICO_APC, panel->bytes, sizeof(panel->bytes));
            gridpico_pico_router_note_sent(&router, GRIDPICO_PICO_APC);
        }
        return;
    }
#endif
    if (submit_host(apc_interface, gridpico_pico_router_led(&router, GRIDPICO_PICO_APC))) {
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
        apc_led_generation = router.generation[GRIDPICO_PICO_APC];
#endif
        gridpico_pico_router_led_sent(&router, GRIDPICO_PICO_APC);
    }
}

static void service_native_midi(void)
{
#ifdef GRIDPICO_RP2040_KEYS_BENCH
    const uint64_t now_us = time_us_64();
    if ((native_note_generation != 0 && now_us >= native_note_deadline) ||
        (router.keys.next_mode != GRIDPICO_APC_CHOOSER_NO_REQUEST && native_led_generation != 0 &&
         now_us >= native_led_deadline)) native_note_fault = true;
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_WEB, tud_mounted() && !native_note_fault);
#else
    gridpico_pico_router_attach(&router, GRIDPICO_PICO_WEB, tud_mounted());
#endif
    if (!router.attached[GRIDPICO_PICO_WEB]) return;
    gridpico_pico_packet packet;
    for (unsigned i = 0; i < RX_BATCH && tud_midi_packet_read(packet.bytes); ++i) {
        receive(GRIDPICO_PICO_WEB, &packet);
    }
    const gridpico_pico_packet *note = gridpico_pico_router_note(&router, GRIDPICO_PICO_WEB);
#ifdef GRIDPICO_RP2040_KEYS_BENCH
    if (note != NULL && native_note_generation == 0 && native_led_generation == 0 &&
        usbd_edpt_ready(DEVICE_ROOT, GRIDPICO_NATIVE_MIDI_IN_EP) && tud_midi_packet_write(note->bytes)) {
        native_note_generation = router.generation[GRIDPICO_PICO_WEB];
        native_note_deadline = time_us_64() + TX_TIMEOUT_US;
#else
    if (note != NULL && tud_midi_packet_write(note->bytes)) {
#endif
        add_trace('G', GRIDPICO_PICO_WEB, note->bytes, sizeof(note->bytes));
        gridpico_pico_router_note_sent(&router, GRIDPICO_PICO_WEB);
    }
    const gridpico_pico_packet *led = gridpico_pico_router_led(&router, GRIDPICO_PICO_WEB);
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    if (note == NULL && led != NULL && native_led_generation == 0 &&
#ifdef GRIDPICO_RP2040_KEYS_BENCH
        native_note_generation == 0 &&
#endif
        usbd_edpt_ready(DEVICE_ROOT, GRIDPICO_NATIVE_MIDI_IN_EP) &&
        tud_midi_packet_write(led->bytes)) {
        native_led_generation = router.generation[GRIDPICO_PICO_WEB];
#ifdef GRIDPICO_RP2040_KEYS_BENCH
        native_led_deadline = time_us_64() + TX_TIMEOUT_US;
#endif
        gridpico_pico_router_led_sent(&router, GRIDPICO_PICO_WEB);
    }
#else
    if (led != NULL && tud_midi_packet_write(led->bytes)) gridpico_pico_router_led_sent(&router, GRIDPICO_PICO_WEB);
#endif

    /* USB configuration does not indicate that a native MIDI reader is listening. */
    const uint64_t now = time_us_64();
    if (native_led_refresh_at == 0) {
        native_led_refresh_at = now + NATIVE_LED_REFRESH_US;
    } else if (now >= native_led_refresh_at) {
        native_led_refresh_at = now + NATIVE_LED_REFRESH_US;
        if (note == NULL && led == NULL && tud_midi_mounted() && !tud_suspended() &&
            usbd_edpt_ready(DEVICE_ROOT, GRIDPICO_NATIVE_MIDI_IN_EP)) {
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
            gridpico_pico_router_offer_led_page(&router, GRIDPICO_PICO_WEB);
#else
            tud_midi_packet_write(router.led.bytes);
#endif
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

#ifdef GRIDPICO_RP2040_ONBOARD_STATUS_BENCH
static uint32_t onboard_first_frame;

/* Keep the LED sample off the router's foreground dispatch stack frame. */
static void __attribute__((noinline)) service_onboard_status(bool host_ready)
{
    static uint64_t next_sample;
    static bool host_advanced;
    const uint64_t now = time_us_64();
    if (now < next_sample) return;
    next_sample = now + 10000;

    gridpico_onboard_facts facts = {.fault = !host_ready};
    if (host_ready) {
        host_advanced |= pio_usb_host_get_frame_number() != onboard_first_frame;
        facts.started = host_advanced;
        facts.midi = (apc_interface != NO_INTERFACE && tuh_midi_mounted(apc_interface)) ||
                     (synth_interface != NO_INTERFACE && tuh_midi_mounted(synth_interface));
        facts.recovery = apc_ambiguous || synth_ambiguous;
        /* Only the selected APC and synth can submit OUT and latch a fault. The
           resolver drops their selections on fault, so inspect the live slots. */
        for (uint8_t i = 0; i < CFG_TUH_MIDI; ++i) {
            facts.fault |= interfaces[i].present && interfaces[i].faulted && tuh_midi_mounted(i);
        }
        /* Pinned TinyUSB allocates hub addresses after its ordinary device slots. */
        for (uint8_t address = CFG_TUH_DEVICE_MAX + 1; address <= CFG_TUH_DEVICE_MAX + CFG_TUH_HUB; ++address) {
            facts.hub |= tuh_mounted(address);
        }
    }
    gridpico_onboard_show(gridpico_onboard_classify(facts));
}
#endif

int main(void)
{
#ifdef GRIDPICO_RP2040_FLASH_ROUTER
    const bool previous_watchdog = watchdog_enable_caused_reboot();
    const uint32_t previous_tag = watchdog_hw->scratch[0];
    const uint32_t previous_complement = watchdog_hw->scratch[1];
    if (gridpico_flash_previous_start_failed(previous_watchdog, previous_tag, previous_complement)) {
        enter_rom();
    }
    gridpico_flash_start_mark(watchdog_hw->scratch);
#endif
    watchdog_enable(WATCHDOG_MS, false);
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
    mark_unused_stack();
#endif
#ifdef GRIDPICO_RP2040_ONBOARD_STATUS_BENCH
    gridpico_onboard_init();
    gridpico_onboard_show(GRIDPICO_ONBOARD_STARTING);
#endif
#ifdef GRIDPICO_RP2040_FLASH_ROUTER
    const uint64_t boot_deadline = time_us_64() + FLASH_START_DEADLINE_US;
    if (!gridpico_flash_init_usb_serial()) enter_rom();
    bool startup_ready = false;
    bool cdc_ready_seen = false;
    uint32_t host_frame_at_start = 0;
#endif
    const tusb_rhport_init_t device = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    if (!tusb_init(DEVICE_ROOT, &device) || gridpico_pico_router_init(&router) != GRIDPICO_OK) {
#ifdef GRIDPICO_RP2040_ONBOARD_STATUS_BENCH
        gridpico_onboard_show(GRIDPICO_ONBOARD_FAULT);
#endif
        enter_rom();
    }
#ifdef GRIDPICO_RP2040_MATRIX_CLI
    bind_matrix_cli();
#endif
#ifdef GRIDPICO_RP2040_FLASH_STARTUP_FAULT
    show_startup_fault(boot_deadline);
#endif

    const uint64_t native_first_until = time_us_64() + NATIVE_WINDOW_US;
    bool host_attempted = false;
    bool host_ready = false;
    for (;;) {
        tud_task_ext(0, false);
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
        gridpico_pico_router_tick(&router, time_us_64());
#endif
#ifdef GRIDPICO_RP2040_FLASH_ROUTER
        if (!startup_ready && tud_cdc_n_ready(0)) cdc_ready_seen = true;
#endif
        service_cdc();
        if (!host_attempted && time_us_64() >= native_first_until) {
            host_attempted = true;
#ifdef GRIDPICO_RP2040_FLASH_ROUTER
            if (time_us_64() >= boot_deadline) enter_rom();
#endif
            watchdog_update();
            host_ready = start_host();
#ifdef GRIDPICO_RP2040_ONBOARD_STATUS_BENCH
            onboard_first_frame = pio_usb_host_get_frame_number();
#endif
#ifdef GRIDPICO_RP2040_FLASH_ROUTER
            if (!host_ready) enter_rom();
            host_frame_at_start = hcd_frame_number(HOST_ROOT);
#endif
        }
        if (host_ready) {
            tuh_task_ext(0, false);
            service_host();
        }
        service_native_midi();
#ifdef GRIDPICO_RP2040_KEYS_BENCH
        gridpico_pico_router_finish_mode(&router,
            !router.attached[GRIDPICO_PICO_WEB] || (native_note_generation == 0 && native_led_generation == 0 &&
                usbd_edpt_ready(DEVICE_ROOT, GRIDPICO_NATIVE_MIDI_IN_EP)),
            apc_interface == NO_INTERFACE || !interfaces[apc_interface].busy,
            synth_interface == NO_INTERFACE || !interfaces[synth_interface].busy);
#endif
#ifdef GRIDPICO_RP2040_FLASH_ROUTER
        if (!startup_ready) {
            const bool host_running = host_ready && hcd_frame_number(HOST_ROOT) != host_frame_at_start;
            startup_ready = check_flash_start(boot_deadline, host_running, cdc_ready_seen);
        }
#endif
#ifdef GRIDPICO_RP2040_ONBOARD_STATUS_BENCH
        if (host_attempted) service_onboard_status(host_ready);
#endif
        watchdog_update();
    }
}
