#include "app/rp2040_router.h"
#include "app/rp2040_auto_routes.h"
#include "bootsel_line.h"

#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pio_usb.h"
#include "tusb.h"
#include "device/usbd_pvt.h"
#include <string.h>

#ifdef GRIDPICO_RP2040_FLASH_ROUTER
#include "flash_startup.h"
#include "hardware/structs/watchdog.h"
#endif
#if defined(GRIDPICO_RP2040_FLASH_ROUTER) || defined(GRIDPICO_PICO_GENERIC_MIDI)
#include "device/dcd.h"
#endif
#ifdef GRIDPICO_PICO_GENERIC_MIDI
#include "hardware/structs/usb.h"
#endif
#if defined(GRIDPICO_RP2040_FLASH_ROUTER) || defined(GRIDPICO_RP2040_PATCHBAY_BENCH)
#include "hardware/sync.h"
#endif
#if defined(GRIDPICO_RP2040_BENCH_TRACE) || defined(GRIDPICO_RP2040_MATRIX_CLI)
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
#ifdef GRIDPICO_RP2040_ONBOARD_STATUS
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

#ifndef GRIDPICO_PICO_GENERIC_MIDI
_Static_assert(GRIDPICO_APC_VID >= 0 && GRIDPICO_APC_VID <= UINT16_MAX &&
               GRIDPICO_APC_PID >= 0 && GRIDPICO_APC_PID <= UINT16_MAX &&
               GRIDPICO_SYNTH_VID >= 0 && GRIDPICO_SYNTH_VID <= UINT16_MAX &&
               GRIDPICO_SYNTH_PID >= 0 && GRIDPICO_SYNTH_PID <= UINT16_MAX,
               "PIO selectors must be 16-bit USB IDs");
_Static_assert(GRIDPICO_APC_INTERFACE >= 0 && GRIDPICO_APC_INTERFACE <= UINT8_MAX &&
               GRIDPICO_SYNTH_INTERFACE >= 0 && GRIDPICO_SYNTH_INTERFACE <= UINT8_MAX,
               "PIO MIDI interfaces must fit in a byte");
#endif

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

#ifdef GRIDPICO_RP2040_ONBOARD_STATUS
_Static_assert(PIO_USB_TX_DEFAULT == 0 && PIO_USB_RX_DEFAULT == 0 &&
               PICO_DEFAULT_WS2812_PIN != HOST_DP && PICO_DEFAULT_WS2812_PIN != HOST_DP + 1,
               "USB PIO0 and onboard status PIO1 require separate hardware");
#endif

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
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    uint32_t out_generation, out_lifetime;
    gridpico_pico_peer out_owner;
    uint8_t next_cable;
#endif
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
#ifndef GRIDPICO_PICO_GENERIC_MIDI
static uint8_t apc_interface = NO_INTERFACE;
static uint8_t synth_interface = NO_INTERFACE;
static bool apc_ambiguous;
static bool synth_ambiguous;
#else
typedef struct {
    uint16_t vid, pid, revision;
    uint8_t interface, input_endpoint, output_endpoint, inputs, outputs;
    char serial[32];
} midi_identity;

typedef struct {
    midi_identity identity;
    char name[GRIDPICO_PATCH_NAME_BYTES];
    uint32_t lifetime;
    uint8_t input_endpoint, input_cables, output_cables;
    bool supported;
    bool examined;
    bool verified;
    bool capacity_reported;
} midi_observation;

typedef struct {
    uint8_t depth;
    uint8_t ports[CFG_TUH_HUB];
} midi_path;

typedef enum { PROBE_LANG, PROBE_NAME, PROBE_SERIAL, PROBE_DONE } probe_phase;

static midi_observation observations[CFG_TUH_MIDI];
static bool devices_ready[CFG_TUH_DEVICE_MAX + 1];
static uint8_t device_streams[CFG_TUH_DEVICE_MAX + 1];
static uint32_t next_midi_lifetime;
static bool discovery_dirty;
typedef struct {
    midi_identity identity;
    uint32_t lifetime;
    gridpico_pico_peer first_peer;
    uint8_t index;
    bool ambiguous;
} discovered_device;
static discovered_device discovered[GRIDPICO_PICO_USB_DEVICES];
static struct {
    uint32_t lifetime;
    uint64_t deadline;
    midi_identity identity;
    midi_path path;
    char name[GRIDPICO_PATCH_NAME_BYTES];
    uint16_t string_data[32];
    uint16_t language;
    uint8_t index, address;
    bool has_name, has_serial, pending, expired;
    probe_phase phase;
} discovery = {.index = NO_INTERFACE};
typedef struct {
    gridpico_revision revision, graph_revision;
    gridpico_port_id panel, led;
    gridpico_port_id suggested_panel, suggested_led;
    uint32_t released_led_generation[GRIDPICO_PICO_USB_CABLE_GROUPS];
    uint32_t panel_generation, led_generation;
    uint32_t led_flight_lifetime;
    gridpico_revision flight_feedback_generation;
    uint8_t led_flight_interface;
    bool retiring, panel_explicit, led_explicit;
} apc_roles;
static apc_roles physical_roles = {.revision = 1};
static gridpico_pico_auto_routes automatic_routes;
static gridpico_result read_apc_roles(void *context, gridpico_apc_roles_status *out);
static gridpico_result apply_apc_role(void *context, const gridpico_apc_role_request *request,
                                     gridpico_apc_roles_status *out);
#endif
#if defined(GRIDPICO_RP2040_PATCHBAY_BENCH) && !defined(GRIDPICO_RP2040_MATRIX_CLI)
enum { CIRCUIT_TRACE_PEER = GRIDPICO_PICO_PEERS };
static uint8_t circuit_interface = NO_INTERFACE;
static bool circuit_ambiguous;
#endif
#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
#ifndef GRIDPICO_PICO_GENERIC_MIDI
static uint32_t apc_led_generation;
#endif
#ifdef GRIDPICO_PICO_GENERIC_MIDI
typedef enum { NATIVE_OUT_IDLE, NATIVE_OUT_NOTE, NATIVE_OUT_PANEL } native_out_kind;
enum { NATIVE_EVENT_IDLE, NATIVE_EVENT_ARMED, NATIVE_EVENT_SEEN, NATIVE_EVENT_DUPLICATE };
static volatile uint8_t native_event_state, native_event_result;
static volatile uint32_t native_event_length;
static volatile uint32_t native_bus_epoch, native_accepted_reset_epoch;
static uint32_t native_mounted_epoch, native_configuration;
static uint32_t native_active_epoch, native_active_configuration;
static bool native_out_started;
static struct {
    uint64_t deadline;
    gridpico_pico_packet packet;
    uint32_t generation;
    native_out_kind kind;
} native_out;
#else
static uint32_t native_led_generation;
#endif
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
#if defined(GRIDPICO_RP2040_FLASH_ROUTER) && defined(GRIDPICO_RP2040_MATRIX_CLI)
enum { TRACE_ROWS = 64 };
#else
enum { TRACE_ROWS = 32 };
#endif
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
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    const uint8_t state[] = {pad.enabled, pad.active, NO_INTERFACE, NO_INTERFACE};
#else
    const uint8_t state[] = {pad.enabled, pad.active, apc_interface, synth_interface};
#endif
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
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    gridpico_cli_bind_apc_roles(&cli, &physical_roles, read_apc_roles, apply_apc_role);
    gridpico_pico_auto_routes_init(&automatic_routes, &router, &routing_scratch);
    gridpico_cli_bind_auto_route_summary(&cli, &automatic_routes, gridpico_pico_auto_routes_read);
    gridpico_cli_bind_auto_route_explain(&cli, &automatic_routes, gridpico_pico_auto_routes_explain);
#endif
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
#endif

#ifdef GRIDPICO_PICO_GENERIC_MIDI
static bool native_bus_loss(uint8_t root, uint32_t event)
{
    return root == DEVICE_ROOT && (event == DCD_EVENT_BUS_RESET || event == DCD_EVENT_UNPLUGGED);
}

void __real_dcd_event_handler(const dcd_event_t *event, bool in_isr);

void __wrap_dcd_event_handler(const dcd_event_t *event, bool in_isr)
{
    if (native_bus_loss(event->rhport, event->event_id)) {
        if (native_bus_epoch != UINT32_MAX) ++native_bus_epoch;
        native_event_state = NATIVE_EVENT_IDLE;
    } else if (event->rhport == DEVICE_ROOT && event->event_id == DCD_EVENT_XFER_COMPLETE &&
               event->xfer_complete.ep_addr == GRIDPICO_NATIVE_MIDI_IN_EP) {
        if (native_event_state == NATIVE_EVENT_ARMED) {
            native_event_result = event->xfer_complete.result;
            native_event_length = event->xfer_complete.len;
            native_event_state = NATIVE_EVENT_SEEN;
        } else if (native_event_state == NATIVE_EVENT_SEEN) {
            native_event_state = NATIVE_EVENT_DUPLICATE;
        }
    }
    __real_dcd_event_handler(event, in_isr);
}

void tud_mount_cb(void)
{
    if (native_configuration != UINT32_MAX) ++native_configuration;
    const uint32_t epoch = native_bus_epoch;
    native_mounted_epoch = epoch == native_accepted_reset_epoch ? epoch : 0;
}

static bool native_endpoint_quiet(void)
{
    _Static_assert(GRIDPICO_NATIVE_MIDI_IN_EP == 0x83, "native MIDI buffer is EP3 IN");
    return (usb_hw->sie_status & USB_SIE_STATUS_BUS_RESET_BITS) == 0 &&
           (usb_hw->buf_status & USB_BUFF_STATUS_EP3_IN_BITS) == 0;
}

static bool native_epoch_current(void)
{
    return native_active_epoch != 0 && native_active_epoch == native_bus_epoch &&
           native_active_epoch == native_mounted_epoch &&
           native_active_configuration == native_configuration && tud_mounted();
}
#endif

#if defined(GRIDPICO_RP2040_FLASH_ROUTER) || defined(GRIDPICO_PICO_GENERIC_MIDI)
void tud_event_hook_cb(uint8_t root, uint32_t event, bool in_isr)
{
    (void)in_isr;
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    /* Only an accepted BUS_RESET can prove an epoch for later reopening. */
    if (root == DEVICE_ROOT && event == DCD_EVENT_BUS_RESET) native_accepted_reset_epoch = native_bus_epoch;
#endif
#ifdef GRIDPICO_RP2040_FLASH_ROUTER
    if (root == DEVICE_ROOT && (event == DCD_EVENT_BUS_RESET || event == DCD_EVENT_SETUP_RECEIVED)) {
        native_bus_activity = true;
    }
#endif
}
#endif

#ifdef GRIDPICO_RP2040_FLASH_ROUTER
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
#ifdef GRIDPICO_RP2040_ONBOARD_STATUS
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
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    native_mounted_epoch = 0;
    native_event_state = NATIVE_EVENT_IDLE;
#elif defined(GRIDPICO_RP2040_PATCHBAY_BENCH)
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

#ifndef GRIDPICO_PICO_GENERIC_MIDI
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

#ifdef GRIDPICO_RP2040_ONBOARD_STATUS
static bool reserved_startup_interface(const host_interface *itf)
{
    return (itf->vid == GRIDPICO_APC_VID && itf->pid == GRIDPICO_APC_PID &&
            itf->number == GRIDPICO_APC_INTERFACE && itf->rx_cables == 2 && itf->tx_cables == 2) ||
           (itf->vid == GRIDPICO_SYNTH_VID && itf->pid == GRIDPICO_SYNTH_PID &&
            itf->number == GRIDPICO_SYNTH_INTERFACE && itf->rx_cables == 1 && itf->tx_cables == 1);
}
#endif
#endif

#ifdef GRIDPICO_PICO_GENERIC_MIDI
static bool observed_path(uint8_t address, midi_path *path)
{
    *path = (midi_path){0};
    for (;;) {
        tuh_bus_info_t bus;
        if (!tuh_bus_info_get(address, &bus) || bus.rhport != HOST_ROOT) return false;
        if (bus.hub_addr == 0) return true;
        if (path->depth == sizeof(path->ports) || bus.hub_port == 0 ||
            bus.hub_addr <= CFG_TUH_DEVICE_MAX || !tuh_mounted(bus.hub_addr)) return false;
        path->ports[path->depth++] = bus.hub_port;
        address = bus.hub_addr;
    }
}

static bool observed_interface(uint8_t index)
{
    const host_interface *itf = &interfaces[index];
    const uint8_t address = itf->device_address;
    const midi_observation *observation = &observations[index];
    return itf->present && observation->lifetime != 0 && observation->supported &&
           itf->rx_cables == observation->input_cables && itf->tx_cables == observation->output_cables &&
           address > 0 && address <= CFG_TUH_DEVICE_MAX &&
           devices_ready[address] && device_streams[address] == 1 && tuh_midi_mounted(index);
}

static bool eligible_interface(uint8_t index)
{
    return !interfaces[index].faulted && observed_interface(index);
}

static bool read_identity(uint8_t index, midi_identity *identity)
{
    const host_interface *itf = &interfaces[index];
    *identity = (midi_identity){
        .vid = itf->vid, .pid = itf->pid,
        .interface = itf->number, .input_endpoint = observations[index].input_endpoint,
        .output_endpoint = host_endpoints_out[index],
        .inputs = (uint8_t)((1u << itf->rx_cables) - 1u),
        .outputs = (uint8_t)((1u << itf->tx_cables) - 1u),
    };
    tusb_desc_device_t descriptor;
    if (!tuh_descriptor_get_device_local(itf->device_address, &descriptor)) return false;
    identity->revision = tu_le16toh(descriptor.bcdDevice);
    return true;
}

static bool same_identity_shape(const midi_identity *left, const midi_identity *right)
{
    return left->vid == right->vid && left->pid == right->pid && left->revision == right->revision &&
           left->interface == right->interface && left->input_endpoint == right->input_endpoint &&
           left->output_endpoint == right->output_endpoint &&
           left->inputs == right->inputs && left->outputs == right->outputs;
}

static bool probe_still_current(void)
{
    const uint8_t index = discovery.index;
    midi_path current;
    return index != NO_INTERFACE && eligible_interface(index) &&
           observations[index].lifetime == discovery.lifetime &&
           interfaces[index].device_address == discovery.address &&
           observed_path(discovery.address, &current) &&
           current.depth == discovery.path.depth &&
           memcmp(current.ports, discovery.path.ports, current.depth) == 0;
}

static void read_usb_text(char *text, size_t capacity, size_t length, bool serial)
{
    const size_t units = length / 2;
    if (units <= 1 || (serial && units > capacity)) return;
    size_t end = units - 1;
    if (end >= capacity) end = capacity - 1;
    bool usable = false;
    for (size_t i = 0; i < end; ++i) {
        const uint16_t ch = tu_le16toh(discovery.string_data[i + 1]);
        if (ch < 32 || ch > 126) return;
        usable |= serial ? ch != ' ' && ch != '0' : ch != ' ';
    }
    if (!usable) return;
    for (size_t i = 0; i < end; ++i) text[i] = (char)tu_le16toh(discovery.string_data[i + 1]);
    text[end] = '\0';
}

static void descriptor_complete(tuh_xfer_t *transfer)
{
    if (!discovery.pending || transfer->user_data != discovery.lifetime || transfer->daddr != discovery.address) {
        return;
    }
    discovery.pending = false;
    if (discovery.expired || time_us_64() >= discovery.deadline) return;
    if (!probe_still_current()) {
        discovery.phase = PROBE_DONE;
        return;
    }

    const uint16_t header = tu_le16toh(discovery.string_data[0]);
    const size_t length = header & 0xffu;
    const bool valid = transfer->result == XFER_RESULT_SUCCESS && header >> 8 == TUSB_DESC_STRING &&
                       length >= 4 && (length & 1u) == 0 && length <= transfer->actual_len &&
                       length <= sizeof(discovery.string_data);
    switch (discovery.phase) {
    case PROBE_LANG:
        if (valid) discovery.language = tu_le16toh(discovery.string_data[1]);
        discovery.phase = discovery.language != 0 ? PROBE_NAME : PROBE_DONE;
        break;
    case PROBE_NAME:
        if (valid) read_usb_text(discovery.name, sizeof(discovery.name), length, false);
        discovery.phase = PROBE_SERIAL;
        break;
    case PROBE_SERIAL:
        if (valid) read_usb_text(discovery.identity.serial, sizeof(discovery.identity.serial), length, true);
        discovery.phase = PROBE_DONE;
        break;
    case PROBE_DONE:
        break;
    }
}

static void start_descriptor_read(void)
{
    memset(discovery.string_data, 0, sizeof(discovery.string_data));
    const uint8_t address = discovery.address;
    const uintptr_t token = discovery.lifetime;
    const uint16_t capacity = sizeof(discovery.string_data);
    switch (discovery.phase) {
    case PROBE_LANG:
        discovery.pending = tuh_descriptor_get_string_langid(address, discovery.string_data, capacity,
                                                             descriptor_complete, token);
        break;
    case PROBE_NAME:
        if (discovery.has_name) {
            discovery.pending = tuh_descriptor_get_product_string(address, discovery.language,
                discovery.string_data, capacity, descriptor_complete, token);
        } else {
            discovery.phase = PROBE_SERIAL;
        }
        break;
    case PROBE_SERIAL:
        if (discovery.has_serial) {
            discovery.pending = tuh_descriptor_get_serial_string(address, discovery.language,
                discovery.string_data, capacity, descriptor_complete, token);
        } else {
            discovery.phase = PROBE_DONE;
        }
        break;
    case PROBE_DONE:
        break;
    }
}

static void report_discovery_capacity(uint8_t index)
{
    if (observations[index].capacity_reported) return;
    observations[index].capacity_reported = true;
    const uint8_t cause = GRIDPICO_CAPACITY;
    add_trace('E', index, &cause, sizeof(cause));
}

static unsigned cable_span(const midi_identity *identity)
{
    return (identity->inputs | identity->outputs) > 1u ? 2u : 1u;
}

static discovered_device *mounted_binding(uint8_t index)
{
    for (size_t slot = 0; slot < GRIDPICO_PICO_USB_DEVICES; ++slot) {
        discovered_device *binding = &discovered[slot];
        if (binding->lifetime != 0 && binding->index == index &&
            binding->lifetime == observations[index].lifetime) return binding;
    }
    return NULL;
}

typedef struct {
    gridpico_pico_peer peer;
    uint8_t index, cable;
} usb_midi_lane;

static gridpico_result lookup_lane_kind(gridpico_port_id id, bool output, bool allow_disputed, usb_midi_lane *lane)
{
    if (id == 0) return GRIDPICO_INVALID;
    for (unsigned p = GRIDPICO_PICO_DISCOVERED; p < GRIDPICO_PICO_PEERS; ++p) {
        const size_t slot = p - GRIDPICO_PICO_DISCOVERED;
        if (id != (output ? router.discovered[slot].output : router.discovered[slot].input)) continue;
        for (size_t d = 0; d < GRIDPICO_PICO_USB_DEVICES; ++d) {
            const discovered_device *binding = &discovered[d];
            const unsigned first = binding->first_peer;
            if (first == 0 || p < first || p >= first + cable_span(&binding->identity)) continue;
            const uint8_t index = binding->index;
            midi_path path;
            if (binding->lifetime == 0 || (binding->ambiguous && !allow_disputed) ||
                !router.attached[p] || mounted_binding(index) != binding ||
                !eligible_interface(index) || !observed_path(interfaces[index].device_address, &path)) {
                return GRIDPICO_UNAVAILABLE;
            }
            *lane = (usb_midi_lane){.peer = (gridpico_pico_peer)p, .index = index, .cable = (uint8_t)(p - first)};
            return GRIDPICO_OK;
        }
    }
    return GRIDPICO_INVALID;
}

static gridpico_result lookup_lane(gridpico_port_id id, bool output, usb_midi_lane *lane)
{
    return lookup_lane_kind(id, output, false, lane);
}

static uint32_t led_role_generation(gridpico_port_id port)
{
    usb_midi_lane lane;
    return lookup_lane(port, true, &lane) == GRIDPICO_OK ? router.generation[lane.peer] : 0;
}

static bool physical_audition_pending(void)
{
    return router.audition.destination != 0 && router.audition.owner == GRIDPICO_PICO_APC;
}

static void attach_physical_panel(apc_roles *roles, uint32_t generation)
{
    if (router.attached[GRIDPICO_PICO_APC]) gridpico_pico_router_attach(&router, GRIDPICO_PICO_APC, false);
    if (generation != 0) gridpico_pico_router_attach(&router, GRIDPICO_PICO_APC, true);
    roles->panel_generation = router.attached[GRIDPICO_PICO_APC] ? generation : 0;
    if (roles->led_generation != 0 && !roles->retiring) {
        gridpico_pico_router_set_physical_feedback(&router, roles->revision);
    }
}

static bool role_has_revision_space(const apc_roles *roles, uint64_t advances)
{
    return advances <= UINT64_MAX - roles->revision;
}

static void record_role_change(apc_roles *roles)
{
    ++roles->revision;
    roles->graph_revision = router.graph.revision;
}

static bool compatible_panel_input(const usb_midi_lane *lane)
{
    const host_interface *interface = &interfaces[lane->index];
    /* The adapter knows this controller protocol on interface 1, cable 1.
     * An explicit logical ID and the current verified lane confer ownership.
     */
    return lane->cable == 1 && interface->number == 1 && interface->rx_cables == 2 &&
           interface->tx_cables == 2 && interface->vid == 0x09e8 && interface->pid == 0x004e;
}

static bool panel_input_fenced(const usb_midi_lane *old, const usb_midi_lane *next)
{
    if (old == NULL && next == NULL) return true;
    /* The pinned MIDI driver submits one IN max-packet per transfer. The
     * single-core PIO alarm accepts it and posts completion before returning.
     * Empty HCD queue and class FIFOs therefore fence all host-received bytes at
     * this sample. Later completions are dispatched after this foreground edit.
     * A busy shared interface defers the edit; do not flush sibling cables.
     */
    const uint32_t irq = save_and_disable_interrupts();
    const bool quiet = !tuh_task_event_ready() &&
        (old == NULL || tuh_midi_read_available(old->index) == 0) &&
        (next == NULL || tuh_midi_read_available(next->index) == 0);
    restore_interrupts(irq);
    return quiet;
}

static uint32_t panel_lane_generation(const usb_midi_lane *lane)
{
    const discovered_device *binding = mounted_binding(lane->index);
    const midi_observation *observation = &observations[lane->index];
    if (binding == NULL || !compatible_panel_input(lane) ||
        (binding->identity.inputs & (1u << lane->cable)) == 0 ||
        (observation->input_endpoint & 0x80u) == 0 ||
        binding->identity.input_endpoint != observation->input_endpoint ||
        binding->identity.interface != interfaces[lane->index].number) return 0;
    return router.discovered[lane->peer - GRIDPICO_PICO_DISCOVERED].input_generation;
}

static bool panel_graph_input(gridpico_port_id id, uint32_t generation, bool reserved)
{
    const gridpico_patch_graph *graph = &router.graph;
    for (size_t i = 0; i < graph->inventory.port_count; ++i) {
        const gridpico_patch_port *port = &graph->inventory.ports[i];
        if (port->id != id) continue;
        return port->direction == GRIDPICO_PATCH_INPUT && port->patch_eligible == !reserved &&
            graph->ports[i].apc_panel_reserved == reserved &&
            graph->port_capabilities[i] == (reserved ? GRIDPICO_LOCAL_PORT_PANEL : GRIDPICO_LOCAL_PORT_MANAGED) &&
            (generation == 0 ? port->state == GRIDPICO_PATCH_DISCONNECTED :
                port->state == GRIDPICO_PATCH_CONNECTED && graph->ports[i].generation == generation);
    }
    return false;
}

static gridpico_result sync_apc_roles(apc_roles *roles)
{
    usb_midi_lane panel_lane;
    uint32_t panel_generation = 0;
    if (lookup_lane(roles->panel, false, &panel_lane) == GRIDPICO_OK) {
        panel_generation = panel_lane_generation(&panel_lane);
    }
    if (panel_generation != 0 &&
        (!panel_graph_input(roles->panel, panel_generation, true) ||
         (roles->panel_generation != panel_generation &&
          (physical_audition_pending() || router.generation[GRIDPICO_PICO_APC] == UINT32_MAX ||
           !panel_input_fenced(NULL, &panel_lane))))) {
        panel_generation = 0;
    }
    const uint32_t led_generation = led_role_generation(roles->led);
    if (panel_generation == roles->panel_generation && led_generation == roles->led_generation &&
        roles->graph_revision == router.graph.revision) return GRIDPICO_OK;

    /* Keep the last revision available for completing an accepted retirement. */
    if (!role_has_revision_space(roles, roles->retiring ? 2 : 1)) return GRIDPICO_CAPACITY;
    record_role_change(roles);
    if (roles->led_generation != led_generation) {
        gridpico_pico_router_set_physical_feedback(&router,
            led_generation != 0 && !roles->retiring ? roles->revision : 0);
        roles->led_generation = led_generation;
    }
    if (roles->panel_generation != panel_generation) attach_physical_panel(roles, panel_generation);
    return GRIDPICO_OK;
}

static void copy_apc_status(const apc_roles *roles, gridpico_apc_roles_status *out)
{
    *out = (gridpico_apc_roles_status){
        .revision = roles->revision, .surface = 1,
        .panel_input = roles->panel, .led_output = roles->led,
        .panel_state = roles->panel == 0 ? GRIDPICO_APC_ROLE_NONE :
            roles->panel_generation != 0 ? GRIDPICO_APC_ROLE_BOUND : GRIDPICO_APC_ROLE_UNRESOLVED,
        .led_state = roles->led == 0 ? GRIDPICO_APC_ROLE_NONE :
            roles->retiring ? GRIDPICO_APC_ROLE_RETIRING :
            roles->led_generation != 0 ? GRIDPICO_APC_ROLE_RESERVED : GRIDPICO_APC_ROLE_UNRESOLVED,
    };
}

static gridpico_result read_apc_roles(void *context, gridpico_apc_roles_status *out)
{
    apc_roles *roles = context;
    if (roles == NULL || out == NULL) return GRIDPICO_INVALID;
    const gridpico_result result = sync_apc_roles(roles);
    if (result == GRIDPICO_OK) copy_apc_status(roles, out);
    return result;
}

static gridpico_result set_led_eligibility(void *graph, gridpico_port_id port, bool reserved)
{
    return gridpico_patch_graph_set_apc_led_reserved(graph, port, reserved);
}

static gridpico_result release_led_role(apc_roles *roles)
{
    if (!role_has_revision_space(roles, 1)) return GRIDPICO_CAPACITY;
    const gridpico_result result = gridpico_patch_session_set_apc_led_reserved(
        &router.session, set_led_eligibility, roles->led, false, roles->led_flight_lifetime == 0);
    if (result != GRIDPICO_OK) return result;
    usb_midi_lane old_lane;
    if (lookup_lane_kind(roles->led, true, true, &old_lane) == GRIDPICO_OK) {
        roles->released_led_generation[old_lane.peer - GRIDPICO_PICO_DISCOVERED] = router.generation[old_lane.peer];
    }
    roles->led = 0;
    roles->led_generation = 0;
    roles->retiring = false;
    record_role_change(roles);
    gridpico_pico_router_set_physical_feedback(&router, 0);
    return GRIDPICO_OK;
}

static gridpico_result change_led_role(apc_roles *roles, gridpico_port_id port, bool mark)
{
    usb_midi_lane lane;
    const gridpico_result lookup = lookup_lane(port, true, &lane);
    if (lookup == GRIDPICO_INVALID) return lookup;
    if (roles->led == port && roles->retiring) return mark ? GRIDPICO_UNAVAILABLE : GRIDPICO_OK;
    if (roles->led != 0 && roles->led != port) return GRIDPICO_UNAVAILABLE;
    if (mark) {
        if (lookup != GRIDPICO_OK) return lookup;
        if (roles->led == port) return GRIDPICO_OK;
        if (!role_has_revision_space(roles, 1)) return GRIDPICO_CAPACITY;
        const bool idle = !interfaces[lane.index].busy && gridpico_pico_router_note(&router, lane.peer) == NULL;
        const gridpico_result result = gridpico_patch_session_set_apc_led_reserved(
            &router.session, set_led_eligibility, port, true, idle);
        if (result != GRIDPICO_OK) return result;
        roles->led = port;
        roles->led_generation = router.generation[lane.peer];
        record_role_change(roles);
        gridpico_pico_router_set_physical_feedback(&router, roles->revision);
        return GRIDPICO_OK;
    }
    if (roles->led == 0) return GRIDPICO_OK;
    if (roles->led_flight_lifetime == 0) return release_led_role(roles);
    if (router.graph.revision == UINT64_MAX || !role_has_revision_space(roles, 2)) return GRIDPICO_CAPACITY;
    roles->retiring = true;
    record_role_change(roles);
    gridpico_pico_router_set_physical_feedback(&router, 0);
    return GRIDPICO_OK;
}

static gridpico_result set_panel_eligibility(void *graph, gridpico_port_id port, bool reserved)
{
    return gridpico_patch_graph_set_apc_panel_reserved(graph, port, reserved);
}

static gridpico_result change_panel_role(apc_roles *roles, gridpico_port_id port)
{
    usb_midi_lane current, next;
    uint32_t next_generation = 0;
    if (port != 0) {
        const gridpico_result result = lookup_lane(port, false, &next);
        if (result != GRIDPICO_OK) return result;
        if (!compatible_panel_input(&next)) return GRIDPICO_INVALID;
        next_generation = panel_lane_generation(&next);
        if (next_generation == 0 || !panel_graph_input(port, next_generation, roles->panel == port)) {
            return GRIDPICO_UNAVAILABLE;
        }
    }
    if (roles->panel == port) {
        return roles->panel_generation == next_generation ? GRIDPICO_OK : GRIDPICO_UNAVAILABLE;
    }
    if (physical_audition_pending()) return GRIDPICO_UNAVAILABLE;
    const bool current_live = lookup_lane_kind(roles->panel, false, true, &current) == GRIDPICO_OK;
    const uint32_t current_generation = current_live ? panel_lane_generation(&current) : 0;
    if (roles->panel != 0 && ((current_live && current_generation == 0) ||
        !panel_graph_input(roles->panel, current_generation, true))) return GRIDPICO_UNAVAILABLE;
    const unsigned graph_steps = (port != 0 ? 1u : 0u) + (roles->panel != 0 ? 1u : 0u) + (current_live ? 2u : 0u);
    if (!role_has_revision_space(roles, roles->retiring ? 2 : 1) || graph_steps > UINT64_MAX - router.graph.revision ||
        (port != 0 && router.generation[GRIDPICO_PICO_APC] == UINT32_MAX) ||
        (current_live && current_generation == UINT32_MAX)) {
        return GRIDPICO_CAPACITY;
    }
    if (!panel_input_fenced(current_live ? &current : NULL, port != 0 ? &next : NULL)) return GRIDPICO_UNAVAILABLE;

    if (port != 0) {
        const gridpico_result result = gridpico_patch_session_set_apc_panel_reserved(
            &router.session, set_panel_eligibility, port, true, true);
        if (result != GRIDPICO_OK) return result;
    }
    if (roles->panel != 0) {
        /* The old claim is tagged and its exact input generation was checked.
         * The reserved source cannot gain managed branches or note debt.
         */
        const gridpico_result result = gridpico_patch_session_set_apc_panel_reserved(
            &router.session, set_panel_eligibility, roles->panel, false, true);
        if (result != GRIDPICO_OK) return result;
        if (current_live) {
            const gridpico_result renewed = gridpico_pico_router_renew_managed_input(&router, current.peer);
            if (renewed != GRIDPICO_OK) return renewed;
        }
    }
    roles->panel = port;
    record_role_change(roles);
    attach_physical_panel(roles, next_generation);
    return GRIDPICO_OK;
}

static gridpico_result apply_apc_role(void *context, const gridpico_apc_role_request *request,
                                     gridpico_apc_roles_status *out)
{
    apc_roles *roles = context;
    if (roles == NULL || request == NULL || out == NULL || request->surface != 1 || request->expected_revision == 0 ||
        request->action < GRIDPICO_APC_ROLE_SET_PANEL || request->action > GRIDPICO_APC_ROLE_UNMARK_LED) {
        return GRIDPICO_INVALID;
    }
    gridpico_result result = sync_apc_roles(roles);
    if (result != GRIDPICO_OK) return result;
    if (roles->revision != request->expected_revision) return GRIDPICO_UNAVAILABLE;
    const bool panel = request->action == GRIDPICO_APC_ROLE_SET_PANEL;
    bool *explicit_owner = panel ? &roles->panel_explicit : &roles->led_explicit;
    if (!*explicit_owner && !role_has_revision_space(roles, roles->retiring ? 2 : 1)) return GRIDPICO_CAPACITY;
    const gridpico_revision before = roles->revision;
    if (panel) result = change_panel_role(roles, request->port);
    else result = change_led_role(roles, request->port, request->action == GRIDPICO_APC_ROLE_MARK_LED);
    if (result == GRIDPICO_OK) {
        if (!*explicit_owner) {
            *explicit_owner = true;
            if (roles->revision == before) record_role_change(roles);
            gridpico_pico_auto_routes_invalidate(&automatic_routes);
        }
        copy_apc_status(roles, out);
    }
    return result;
}

static void service_apc_roles(void)
{
    apc_roles *roles = &physical_roles;
    if (roles->retiring && roles->led_flight_lifetime == 0) {
        (void)release_led_role(roles);
    }
    (void)sync_apc_roles(roles);
}

static bool same_serial(const midi_identity *left, const midi_identity *right)
{
    return same_identity_shape(left, right) && left->serial[0] != '\0' &&
           strcmp(left->serial, right->serial) == 0;
}

static bool identity_conflict(uint8_t index, bool *unresolved)
{
    for (uint8_t i = 0; i < CFG_TUH_MIDI; ++i) {
        if (i == index || !observed_interface(i)) continue;
        const midi_identity *identity = &observations[index].identity;
        const midi_observation *other = &observations[i];
        if (other->verified) {
            if (same_serial(&other->identity, identity)) return true;
            *unresolved |= other->identity.serial[0] == '\0' && same_identity_shape(&other->identity, identity);
        } else {
            midi_identity shape;
            if (!read_identity(i, &shape)) shape.revision = identity->revision;
            *unresolved |= same_identity_shape(&shape, identity);
        }
    }
    return false;
}

static void bind_discovered(discovered_device *binding, uint8_t index)
{
    const unsigned span = cable_span(&binding->identity);
    for (unsigned cable = 0; cable < span; ++cable) {
        const unsigned peer = binding->first_peer + cable;
        if (router.generation[peer] == UINT32_MAX ||
            router.discovered[peer - GRIDPICO_PICO_DISCOVERED].input_generation == UINT32_MAX) {
            report_discovery_capacity(index);
            return;
        }
    }
    binding->index = index;
    binding->lifetime = observations[index].lifetime;
    for (unsigned cable = 0; cable < span; ++cable) {
        gridpico_pico_router_attach(&router, (gridpico_pico_peer)(binding->first_peer + cable), true);
    }
}

static void admit_discovered(void)
{
    uint8_t new_mounts[CFG_TUH_MIDI];
    size_t new_count = 0;
    unsigned free_devices = GRIDPICO_PICO_USB_DEVICES;
    /* Allocate contiguous cable groups in append order. Slots of retired
       devices stay reserved, so a return never inherits another queue. */
    unsigned next_peer = GRIDPICO_PICO_DISCOVERED;
    for (size_t slot = 0; slot < GRIDPICO_PICO_USB_DEVICES; ++slot) {
        const discovered_device *binding = &discovered[slot];
        if (binding->first_peer == 0) break;
        --free_devices;
        next_peer += cable_span(&binding->identity);
    }

    unsigned new_cables = 0;
    for (uint8_t index = 0; index < CFG_TUH_MIDI; ++index) {
        if (!observations[index].verified || !eligible_interface(index)) continue;
        const midi_identity *identity = &observations[index].identity;
        bool unresolved = false;
        const bool duplicate = identity_conflict(index, &unresolved);
        const bool bound = mounted_binding(index) != NULL;
        bool reserved = duplicate;
        for (size_t slot = 0; slot < GRIDPICO_PICO_USB_DEVICES; ++slot) {
            discovered_device *binding = &discovered[slot];
            if (binding->first_peer == 0 || !same_identity_shape(&binding->identity, identity)) continue;
            if (same_serial(&binding->identity, identity)) {
                reserved = true;
                binding->ambiguous |= duplicate;
                if (!bound && binding->lifetime == 0 && !binding->ambiguous && !unresolved) {
                    bind_discovered(binding, index);
                }
                break;
            }
            if (binding->lifetime == 0 &&
                (identity->serial[0] == '\0' || binding->identity.serial[0] == '\0')) reserved = true;
        }
        if (bound || mounted_binding(index) != NULL) continue;
        if (reserved) report_discovery_capacity(index);
        else {
            new_mounts[new_count++] = index;
            new_cables += cable_span(identity);
        }
    }

    /* Do not pick a winner by discovery order when the batch does not fit. */
    const bool fits = new_count <= free_devices && next_peer + new_cables <= GRIDPICO_PICO_PEERS;
    for (size_t i = 0; i < new_count; ++i) {
        const uint8_t index = new_mounts[i];
        if (!fits) {
            report_discovery_capacity(index);
            continue;
        }
        const midi_observation *observation = &observations[index];
        const midi_identity *identity = &observation->identity;
        const gridpico_pico_peer first_peer = (gridpico_pico_peer)next_peer;
        const gridpico_result result = gridpico_pico_router_register_device(
            &router, first_peer, observation->name, identity->inputs, identity->outputs);
        if (result != GRIDPICO_OK) {
            const uint8_t cause = (uint8_t)result;
            add_trace('E', index, &cause, sizeof(cause));
            continue;
        }
        discovered_device *binding = &discovered[GRIDPICO_PICO_USB_DEVICES - free_devices--];
        *binding = (discovered_device){.identity = *identity, .first_peer = first_peer};
        next_peer += cable_span(identity);
        bind_discovered(binding, index);
    }
}

static void remember_observation(uint8_t index, const midi_identity *identity, const char *name)
{
    midi_observation *observation = &observations[index];
    observation->identity = *identity;
    if (name != NULL && name[0] != '\0') {
        memcpy(observation->name, name, sizeof(observation->name));
    } else {
        snprintf(observation->name, sizeof(observation->name), "USB MIDI %04X:%04X",
                 (unsigned)identity->vid, (unsigned)identity->pid);
    }
    observation->verified = true;
    observation->examined = true;
}

static void admit_without_strings(void)
{
    if (!discovery_dirty) return;
    discovery_dirty = false;
    for (uint8_t index = 0; index < CFG_TUH_MIDI; ++index) {
        if (!eligible_interface(index) || observations[index].verified) continue;
        midi_identity identity;
        midi_path path;
        if (read_identity(index, &identity) && observed_path(interfaces[index].device_address, &path)) {
            remember_observation(index, &identity, NULL);
        }
    }
    admit_discovered();
}

static void service_discovery(void)
{
    bool just_expired = false;
    if (discovery.index != NO_INTERFACE && !discovery.expired && time_us_64() >= discovery.deadline) {
        discovery.expired = true;
        discovery_dirty = true;
        just_expired = true;
    }
    if (discovery.expired) {
        /* Abort only our active request if the same lifetime still owns the
         * address. A queued request can still borrow our buffer after timeout.
         */
        if (discovery.pending && probe_still_current()) {
            (void)tuh_edpt_abort_xfer(discovery.address, 0);
        }
        if (discovery.pending || just_expired) admit_without_strings();
        if (!discovery.pending) {
            discovery.index = NO_INTERFACE;
            discovery.expired = false;
        }
        return;
    }
    if (discovery.pending) return;

    if (discovery.index != NO_INTERFACE) {
        const uint8_t index = discovery.index;
        if (probe_still_current()) {
            if (discovery.phase != PROBE_DONE) {
                start_descriptor_read();
                return;
            }
            remember_observation(index, &discovery.identity, discovery.name);
        } else if (observations[index].lifetime == discovery.lifetime) {
            observations[index].examined = false;
        }
        discovery.index = NO_INTERFACE;
        discovery_dirty = true;
        return;
    }
    if (!discovery_dirty) return;
    discovery_dirty = false;

    for (uint8_t index = 0; index < CFG_TUH_MIDI; ++index) {
        if (observations[index].examined || !eligible_interface(index)) continue;
        const uint8_t address = interfaces[index].device_address;
        tusb_desc_device_t descriptor;
        if (!read_identity(index, &discovery.identity) || !observed_path(address, &discovery.path) ||
            !tuh_descriptor_get_device_local(address, &descriptor)) continue;
        observations[index].examined = true;
        discovery.index = index;
        discovery.address = address;
        discovery.lifetime = observations[index].lifetime;
        discovery.has_name = descriptor.iProduct != 0;
        discovery.has_serial = descriptor.iSerialNumber != 0;
        discovery.language = 0;
        discovery.name[0] = '\0';
        discovery.phase = discovery.has_name || discovery.has_serial ? PROBE_LANG : PROBE_DONE;
        discovery.deadline = time_us_64() + TX_TIMEOUT_US;
        return;
    }
    admit_discovered();
}

static bool recognizable_controller(uint8_t index)
{
    const usb_midi_lane control = {.index = index, .cable = 1};
    midi_path path;
    return observed_interface(index) && compatible_panel_input(&control) &&
        observed_path(interfaces[index].device_address, &path);
}

static bool placed_port(const gridpico_port_id *placements, size_t count, gridpico_port_id port)
{
    for (size_t i = 0; i < count; ++i) {
        if (placements[i] == port) return true;
    }
    return false;
}

static bool try_automatic_roles(apc_roles *roles, gridpico_port_id panel, gridpico_port_id led)
{
    const gridpico_apc_patch_layout *layout = &router.session.apc_layout;
    bool claimed = false;
    if (led != 0 && roles->led == 0 && !roles->led_explicit &&
        !placed_port(layout->columns, GRIDPICO_PATCH_DESTINATION_COLUMNS, led)) {
        claimed = change_led_role(roles, led, true) == GRIDPICO_OK;
    }
    if (panel != 0 && roles->panel == 0 && !roles->panel_explicit &&
        !placed_port(layout->rows, GRIDPICO_PATCH_SOURCE_ROWS, panel) &&
        change_panel_role(roles, panel) == GRIDPICO_OK) {
        claimed = true;
    }
    return claimed;
}

static void nominate_controller(apc_roles *roles)
{
    if ((roles->panel != 0 || roles->panel_explicit) && (roles->led != 0 || roles->led_explicit)) return;
    if (roles->suggested_panel != 0 || roles->suggested_led != 0) {
        (void)try_automatic_roles(roles, roles->suggested_panel, roles->suggested_led);
        return;
    }

    uint8_t ordered[CFG_TUH_MIDI];
    size_t count = 0;
    bool unidentified = false;
    for (uint8_t index = 0; index < CFG_TUH_MIDI; ++index) {
        if (!recognizable_controller(index)) continue;
        const midi_observation *observation = &observations[index];
        unidentified |= !observation->verified || observation->identity.serial[0] == '\0';
        for (size_t i = 0; i < count; ++i) {
            if (same_serial(&observation->identity, &observations[ordered[i]].identity)) return;
        }
        size_t position = count++;
        while (position > 0 && observations[ordered[position - 1]].lifetime > observation->lifetime) {
            ordered[position] = ordered[position - 1];
            --position;
        }
        ordered[position] = index;
    }
    if (unidentified && count > 1) return;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t index = ordered[i];
        const discovered_device *binding = mounted_binding(index);
        if (binding == NULL || binding->ambiguous || !eligible_interface(index)) continue;
        const size_t slot = (size_t)(binding->first_peer - GRIDPICO_PICO_DISCOVERED) + 1;
        const gridpico_port_id panel = router.discovered[slot].input;
        const gridpico_port_id led = router.discovered[slot].output;
        if (try_automatic_roles(roles, panel, led)) {
            /* Preserve continuity only after this controller actually owns a
             * role. An unsuccessful suggestion cannot displace a free one.
             */
            roles->suggested_panel = panel;
            roles->suggested_led = led;
            return;
        }
    }
}

static void service_automatic_policy(void)
{
    const bool coherent = !discovery_dirty && (discovery.index == NO_INTERFACE || discovery.expired) &&
                          sync_apc_roles(&physical_roles) == GRIDPICO_OK;
    apc_roles *roles = &physical_roles;
    uint32_t blocked_inputs = 0;
    uint32_t blocked_outputs = 0;
    if (coherent) {
        nominate_controller(roles);
        /* An unresolved control lane is not automatically given a musical
         * route while the candidate role is still optional. Explicit selection
         * and explicit role release retain their own transport ownership.
         */
        for (uint8_t index = 0; index < CFG_TUH_MIDI; ++index) {
            const discovered_device *binding = mounted_binding(index);
            if (binding == NULL || !recognizable_controller(index)) continue;
            const unsigned slot = binding->first_peer - GRIDPICO_PICO_DISCOVERED + 1u;
            if (!roles->panel_explicit) blocked_inputs |= UINT32_C(1) << slot;
            if (!roles->led_explicit) blocked_outputs |= UINT32_C(1) << slot;
        }
        for (unsigned slot = 0; slot < GRIDPICO_PICO_USB_CABLE_GROUPS; ++slot) {
            const uint32_t released = roles->released_led_generation[slot];
            if (released != 0 && released == router.generation[GRIDPICO_PICO_DISCOVERED + slot]) {
                blocked_outputs |= UINT32_C(1) << slot;
            }
        }
    }
    gridpico_pico_auto_routes_service(&automatic_routes, coherent, blocked_inputs, blocked_outputs);
}

static void retire_host_interface(uint8_t index, bool fault)
{
    host_interface *itf = &interfaces[index];
    discovered_device *binding = mounted_binding(index);
    if (binding != NULL) binding->lifetime = 0;
    discovery_dirty = true;
    if (itf->busy) {
        fault |= !tuh_edpt_abort_xfer(itf->device_address, host_endpoints_out[index]);
        itf->busy = false;
        itf->out_generation = 0;
    }
    itf->out_lifetime = 0;
    itf->faulted |= fault;
    if (binding != NULL) {
        for (unsigned cable = 0; cable < cable_span(&binding->identity); ++cable) {
            const gridpico_pico_peer peer = (gridpico_pico_peer)(binding->first_peer + cable);
            const size_t slot = peer - GRIDPICO_PICO_DISCOVERED;
            if (physical_roles.panel_generation != 0 &&
                router.discovered[slot].input == physical_roles.panel &&
                router.discovered[slot].input_generation == physical_roles.panel_generation) {
                gridpico_pico_router_cancel_audition_owner(&router, GRIDPICO_PICO_APC);
            }
            gridpico_pico_router_attach(&router, peer, false);
        }
    }
}
#endif

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
#endif

#if defined(GRIDPICO_RP2040_PIO_DIAGNOSTICS) || defined(GRIDPICO_PICO_GENERIC_MIDI)
bool tuh_enum_descriptor_configuration_cb(uint8_t address, uint8_t index,
                                          const tusb_desc_configuration_t *descriptor)
{
    const uint16_t length = tu_le16toh(descriptor->wTotalLength);
#ifdef GRIDPICO_RP2040_PIO_DIAGNOSTICS
    const uint8_t values[] = {index, descriptor->bNumInterfaces, (uint8_t)(length >> 8), (uint8_t)length};
    add_trace('C', address, values, sizeof(values));
#else
    (void)index;
#endif
#ifdef GRIDPICO_RP2040_HUB_TRACE
    gridpico_hub_trace_config(address);
#endif
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    if (address > 0 && address <= CFG_TUH_DEVICE_MAX) {
        device_streams[address] = 0;
        /* TinyUSB checked the short header. Validate the new full header
         * before either walker uses its length. This callback has no actual
         * transfer length with which to detect a short response.
         */
        if (descriptor->bDescriptorType != TUSB_DESC_CONFIGURATION ||
            descriptor->bLength != sizeof(*descriptor) ||
            length < sizeof(*descriptor) || length > CFG_TUH_ENUMERATION_BUFSIZE) {
            return false;
        }

        uint8_t streams = 0;
        const uint8_t *bytes = (const uint8_t *)descriptor;
        size_t offset = sizeof(*descriptor);
        while (offset + 2 <= length && bytes[offset] >= 2 && offset + bytes[offset] <= length) {
            const uint8_t *entry = bytes + offset;
            if (entry[1] == TUSB_DESC_INTERFACE) {
                if (entry[0] < sizeof(tusb_desc_interface_t)) return false;
                if (entry[3] == 0 && entry[5] == TUSB_CLASS_AUDIO && entry[6] == AUDIO_SUBCLASS_MIDI_STREAMING) {
                    ++streams;
                }
            } else if (entry[1] == TUSB_DESC_ENDPOINT && entry[0] < sizeof(tusb_desc_endpoint_t)) {
                return false;
            }
            offset += entry[0];
        }
        if (offset != length) return false;
        device_streams[address] = streams;
    }
#endif
    return true;
}

void tuh_mount_cb(uint8_t address)
{
#ifdef GRIDPICO_RP2040_PIO_DIAGNOSTICS
    tuh_bus_info_t bus;
    tuh_bus_info_get(address, &bus);
    const uint8_t values[] = {bus.hub_addr, bus.hub_port};
    add_trace('N', address, values, sizeof(values));
#endif
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    if (address > 0 && address <= CFG_TUH_DEVICE_MAX) devices_ready[address] = true;
    discovery_dirty = true;
#endif
}
#endif

#ifdef GRIDPICO_PICO_GENERIC_MIDI
void tuh_umount_cb(uint8_t address)
{
    if (address > 0 && address <= CFG_TUH_DEVICE_MAX) {
        devices_ready[address] = false;
        device_streams[address] = 0;
    }
    for (uint8_t index = 0; index < CFG_TUH_MIDI; ++index) {
        if (interfaces[index].present && interfaces[index].device_address == address) {
            retire_host_interface(index, false);
        }
    }
    discovery_dirty = true;
}

static bool midi_data_endpoint(const tusb_desc_endpoint_t *endpoint, tusb_dir_t direction)
{
    if (endpoint == NULL) return false;
    const uint8_t address = endpoint->bEndpointAddress;
    const uint16_t packet_size = tu_edpt_packet_size(endpoint);
    return tu_edpt_dir(address) == direction && tu_edpt_number(address) != 0 &&
           (address & 0x70u) == 0 && packet_size >= GRIDPICO_PICO_PACKET_BYTES &&
           packet_size <= CFG_TUH_MIDI_EP_BUFSIZE &&
           (endpoint->bmAttributes.xfer == TUSB_XFER_BULK ||
            endpoint->bmAttributes.xfer == TUSB_XFER_INTERRUPT);
}
#endif

#ifdef GRIDPICO_PICO_GENERIC_MIDI
static uint8_t endpoint_cables(const tusb_desc_interface_t *interface, uint16_t total_length,
                               const tusb_desc_endpoint_t *endpoint, tusb_dir_t direction)
{
    if (!midi_data_endpoint(endpoint, direction)) return 0;
    const uint8_t *entry = (const uint8_t *)endpoint;
    const uint8_t *start = (const uint8_t *)interface;
    const size_t offset = (size_t)(entry - start) + endpoint->bLength;
    if (offset + 4u > total_length) return 0;
    const uint8_t *jacks = start + offset;
    const uint8_t count = jacks[3];
    if (jacks[1] != TUSB_DESC_CS_ENDPOINT || jacks[2] != 1 ||
        count == 0 || count > GRIDPICO_PICO_USB_CABLES ||
        jacks[0] < 4u + count || offset + jacks[0] > total_length) return 0;
    for (uint8_t i = 0; i < count; ++i) {
        if (jacks[4u + i] == 0 || (i > 0 && jacks[4u + i] == jacks[4u])) return 0;
    }
    return count;
}
#endif

void tuh_midi_descriptor_cb(uint8_t index, const tuh_midi_descriptor_cb_t *descriptor)
{
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    if (index < CFG_TUH_MIDI && !interfaces[index].present) {
        host_endpoints_out[index] = descriptor->desc_epout == NULL ? 0 : descriptor->desc_epout->bEndpointAddress;
        const tusb_desc_interface_t *midi = descriptor->desc_midi;
        const uint8_t *header = descriptor->desc_header;
        observations[index] = (midi_observation){0};
        if (midi != NULL && midi->bAlternateSetting == 0 &&
            midi->bInterfaceClass == TUSB_CLASS_AUDIO && midi->bInterfaceSubClass == AUDIO_SUBCLASS_MIDI_STREAMING &&
            header != NULL && header[0] >= 7 && header[3] == 0 && header[4] == 1) {
            const uint8_t rx = endpoint_cables(midi, descriptor->desc_midi_total_len, descriptor->desc_epin, TUSB_DIR_IN);
            const uint8_t tx = endpoint_cables(midi, descriptor->desc_midi_total_len, descriptor->desc_epout, TUSB_DIR_OUT);
            const bool have_rx = descriptor->desc_epin != NULL;
            const bool have_tx = descriptor->desc_epout != NULL;
            if (have_rx + have_tx == midi->bNumEndpoints && midi->bNumEndpoints != 0 &&
                (rx != 0) == have_rx && (tx != 0) == have_tx) {
                observations[index] = (midi_observation){
                    .supported = true, .input_cables = rx, .output_cables = tx,
                    .input_endpoint = have_rx ? descriptor->desc_epin->bEndpointAddress : 0,
                };
            }
        }
    }
#else
    if (index < CFG_TUH_MIDI && descriptor->desc_epout != NULL) {
        host_endpoints_out[index] = descriptor->desc_epout->bEndpointAddress;
    }
#endif
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
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    retire_host_interface(index, false);
    if (physical_roles.led_flight_interface == index &&
        physical_roles.led_flight_lifetime == observations[index].lifetime) {
        /* The next foreground service runs after TinyUSB finishes closing the class. */
        physical_roles.led_flight_lifetime = 0;
    }
    observations[index] = (midi_observation){0};
    discovery_dirty = true;
#endif
    interfaces[index] = (host_interface){0};
    host_endpoints_out[index] = 0;
#ifndef GRIDPICO_PICO_GENERIC_MIDI
    reconcile_host();
#endif
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
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    if (next_midi_lifetime != UINT32_MAX) observations[index].lifetime = ++next_midi_lifetime;
    discovery_dirty = true;
#endif
    const uint8_t identity[] = {(uint8_t)(vid >> 8), (uint8_t)vid, (uint8_t)(pid >> 8), (uint8_t)pid,
                                mount->bInterfaceNumber, mount->rx_cable_count, mount->tx_cable_count};
    add_trace('M', index, identity, sizeof(identity));
#ifndef GRIDPICO_PICO_GENERIC_MIDI
    reconcile_host();
#endif
}

#ifdef GRIDPICO_RP2040_HCD_TRACE
bool __real_midih_xfer_cb(uint8_t address, uint8_t endpoint, xfer_result_t result, uint32_t transferred);

#ifdef GRIDPICO_PICO_GENERIC_MIDI
static bool current_musical_flight(uint8_t index)
{
    const host_interface *itf = &interfaces[index];
    const discovered_device *binding = mounted_binding(index);
    const gridpico_pico_peer owner = itf->out_owner;
    const unsigned cable = itf->in_flight.bytes[0] >> 4;
    if (binding == NULL || owner < GRIDPICO_PICO_DISCOVERED || owner >= GRIDPICO_PICO_PEERS ||
        cable >= cable_span(&binding->identity) || (binding->identity.outputs & (1u << cable)) == 0 ||
        owner != binding->first_peer + cable || !router.attached[owner] ||
        itf->out_generation != router.generation[owner]) return false;

    const gridpico_pico_packet *head = gridpico_pico_router_note(&router, owner);
    gridpico_pico_packet sent = itf->in_flight;
    sent.bytes[0] &= 0x0fu;
    return head != NULL && memcmp(head->bytes, sent.bytes, sizeof(sent.bytes)) == 0;
}

bool __wrap_midih_xfer_cb(uint8_t address, uint8_t endpoint, xfer_result_t result, uint32_t transferred)
{
    uint8_t matched = NO_INTERFACE;
    if (address != 0 && endpoint != 0 && tu_edpt_dir(endpoint) == TUSB_DIR_OUT) {
        for (uint8_t index = 0; index < CFG_TUH_MIDI; ++index) {
            const host_interface *itf = &interfaces[index];
            if (!itf->present || !itf->busy || itf->faulted || itf->out_lifetime == 0 ||
                itf->out_lifetime != observations[index].lifetime ||
                itf->device_address != address || host_endpoints_out[index] != endpoint) continue;
            if (matched != NO_INTERFACE) {
                matched = NO_INTERFACE;
                break;
            }
            matched = index;
        }
    }
    const bool handled = __real_midih_xfer_cb(address, endpoint, result, transferred);
    if (matched != NO_INTERFACE) {
        host_interface *itf = &interfaces[matched];
        const gridpico_pico_peer owner = itf->out_owner;
        const bool led = owner == GRIDPICO_PICO_APC;
        const bool current = led ? physical_roles.led_flight_interface == matched &&
            physical_roles.led_flight_lifetime == observations[matched].lifetime :
            current_musical_flight(matched);
        const bool complete = current && handled && result == XFER_RESULT_SUCCESS &&
            transferred == GRIDPICO_PICO_PACKET_BYTES;
        const uint8_t values[] = {address, endpoint, (uint8_t)result, (uint8_t)(transferred >> 24),
                                  (uint8_t)(transferred >> 16), (uint8_t)(transferred >> 8), (uint8_t)transferred};
        add_trace('H', matched, values, sizeof(values));
        add_trace(complete ? 'T' : 'F', matched, itf->in_flight.bytes, sizeof(itf->in_flight.bytes));
        itf->busy = false;
        itf->out_generation = 0;
        if (led && current) {
            physical_roles.led_flight_lifetime = 0;
            if (physical_roles.flight_feedback_generation == router.physical_feedback_generation) {
                gridpico_pico_router_led_complete(&router, GRIDPICO_PICO_APC, complete);
            }
        }
        if (complete && !led) {
            gridpico_pico_router_note_sent(&router, owner);
        }
        if (!complete) retire_host_interface(matched, true);
    }
    return handled;
}
#else
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
#endif

#ifdef GRIDPICO_RP2040_PATCHBAY_BENCH
bool __real_midid_xfer_cb(uint8_t root, uint8_t endpoint, xfer_result_t result, uint32_t transferred);

bool __wrap_midid_xfer_cb(uint8_t root, uint8_t endpoint, xfer_result_t result, uint32_t transferred)
{
    const bool handled = __real_midid_xfer_cb(root, endpoint, result, transferred);
#ifdef GRIDPICO_PICO_GENERIC_MIDI
    if (root != DEVICE_ROOT || endpoint != GRIDPICO_NATIVE_MIDI_IN_EP ||
        native_out.kind == NATIVE_OUT_IDLE) return handled;

    const uint32_t irq = save_and_disable_interrupts();
    const bool owned = native_epoch_current() && !tud_suspended() && native_endpoint_quiet() &&
        native_out.generation == router.generation[GRIDPICO_PICO_WEB] &&
        native_event_state >= NATIVE_EVENT_SEEN;
    const bool complete = owned && native_event_state == NATIVE_EVENT_SEEN &&
        native_event_result == result && native_event_length == transferred &&
        handled && result == XFER_RESULT_SUCCESS && transferred == GRIDPICO_PICO_PACKET_BYTES;
    const native_out_kind kind = native_out.kind;
    if (owned) {
        native_event_state = NATIVE_EVENT_IDLE;
        native_out.kind = NATIVE_OUT_IDLE;
    }
    restore_interrupts(irq);
    if (!owned) return handled;

    if (!complete) {
        gridpico_pico_router_retire_native_output(&router);
    } else if (kind == NATIVE_OUT_NOTE) {
        const gridpico_pico_packet *head = gridpico_pico_router_note(&router, GRIDPICO_PICO_WEB);
        if (head != NULL && memcmp(head->bytes, native_out.packet.bytes, sizeof(native_out.packet.bytes)) == 0) {
            gridpico_pico_router_note_sent(&router, GRIDPICO_PICO_WEB);
        } else {
            gridpico_pico_router_retire_native_output(&router);
        }
    } else {
        gridpico_pico_router_led_complete(&router, GRIDPICO_PICO_WEB, true);
    }
#else
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
#endif
    return handled;
}
#endif

#ifdef GRIDPICO_PICO_GENERIC_MIDI
void tuh_midi_tx_cb(uint8_t index, uint32_t transferred)
{
    /* The class hook owns the complete result for direct transfers. */
    (void)index;
    (void)transferred;
}

static bool submit_host_packet(uint8_t index, gridpico_pico_peer owner, const gridpico_pico_packet *packet,
                               uint8_t cable)
{
    host_interface *itf = &interfaces[index];
    if (itf->busy || !eligible_interface(index) || cable >= itf->tx_cables ||
        (owner == GRIDPICO_PICO_APC ? router.physical_feedback_generation == 0 : !router.attached[owner])) {
        return false;
    }
    const uint32_t lifetime = observations[index].lifetime;
    if (itf->out_lifetime != lifetime) {
        /* The PIO frame IRQ posts completions on this core before it returns.
         * The retired device's endpoints are closed before reuse. Drain its
         * queued events before the first flight of a replacement lifetime.
         */
        const uint32_t irq = save_and_disable_interrupts();
        const bool drained = !tuh_task_event_ready();
        restore_interrupts(irq);
        if (!drained) return false;
    }
    itf->in_flight = *packet;
    itf->in_flight.bytes[0] = (uint8_t)((packet->bytes[0] & 0x0fu) | ((unsigned)cable << 4));
    tuh_xfer_t transfer = {
        .daddr = itf->device_address,
        .ep_addr = host_endpoints_out[index],
        .buflen = sizeof(itf->in_flight.bytes),
        .buffer = itf->in_flight.bytes,
    };
    /* A refused claim cannot leave bytes in TinyUSB's class FIFO. */
    if (!tuh_edpt_xfer(&transfer)) return false;
    itf->out_owner = owner;
    itf->out_generation = router.generation[owner];
    itf->out_lifetime = lifetime;
    itf->busy = true;
    itf->deadline = time_us_64() + TX_TIMEOUT_US;
    return true;
}

static bool service_musical_out(uint8_t index, const discovered_device *binding)
{
    host_interface *itf = &interfaces[index];
    const unsigned span = cable_span(&binding->identity);
    for (unsigned offset = 0; offset < span; ++offset) {
        const uint8_t cable = (uint8_t)((itf->next_cable + offset) % span);
        if ((binding->identity.outputs & (1u << cable)) == 0) continue;
        const gridpico_pico_peer owner = (gridpico_pico_peer)(binding->first_peer + cable);
        const gridpico_pico_packet *packet = gridpico_pico_router_note(&router, owner);
        if (packet == NULL) continue;
        if (submit_host_packet(index, owner, packet, cable)) {
            add_trace('G', (uint8_t)owner, itf->in_flight.bytes, sizeof(itf->in_flight.bytes));
            itf->next_cable = (uint8_t)((cable + 1u) % span);
        }
        return true;
    }
    return false;
}
static void service_apc_led_out(uint8_t index)
{
    apc_roles *roles = &physical_roles;
    if (roles->led_generation == 0 || roles->retiring || roles->led_flight_lifetime != 0) return;
    usb_midi_lane lane;
    if (lookup_lane(roles->led, true, &lane) != GRIDPICO_OK || lane.index != index ||
        roles->led_generation != router.generation[lane.peer]) return;
    const gridpico_pico_packet *packet = gridpico_pico_router_led(&router, GRIDPICO_PICO_APC);
    if (packet != NULL && submit_host_packet(index, GRIDPICO_PICO_APC, packet, lane.cable)) {
        roles->led_flight_interface = index;
        roles->led_flight_lifetime = observations[index].lifetime;
        roles->flight_feedback_generation = router.physical_feedback_generation;
        gridpico_pico_router_led_sent(&router, GRIDPICO_PICO_APC);
    }
}
#else
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

#endif

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

#ifdef GRIDPICO_PICO_GENERIC_MIDI
static void service_host(void)
{
    for (uint8_t index = 0; index < CFG_TUH_MIDI; ++index) {
        host_interface *itf = &interfaces[index];
        if (!itf->present || !tuh_midi_mounted(index) || itf->faulted) continue;
        if (itf->busy && time_us_64() >= itf->deadline) {
            add_trace('F', index, itf->in_flight.bytes, sizeof(itf->in_flight.bytes));
            retire_host_interface(index, true);
            continue;
        }
        const discovered_device *binding = mounted_binding(index);
        gridpico_pico_packet packet;
        for (unsigned i = 0; i < RX_BATCH && tuh_midi_packet_read(index, packet.bytes); ++i) {
            const uint8_t cable = packet.bytes[0] >> 4;
            if (binding != NULL && cable < GRIDPICO_PICO_USB_CABLES &&
                (binding->identity.inputs & (1u << cable)) != 0) {
                const gridpico_pico_peer peer = (gridpico_pico_peer)(binding->first_peer + cable);
                const size_t slot = peer - GRIDPICO_PICO_DISCOVERED;
                if (router.discovered[slot].input == physical_roles.panel) {
                    if (!binding->ambiguous && physical_roles.panel_generation != 0 &&
                        physical_roles.panel_generation == router.discovered[slot].input_generation &&
                        router.attached[GRIDPICO_PICO_APC]) {
                        packet.bytes[0] = (uint8_t)((packet.bytes[0] & 0x0fu) | 0x10u);
                        receive(GRIDPICO_PICO_APC, &packet);
                    }
                } else {
                    packet.bytes[0] &= 0x0fu;
                    receive(peer, &packet);
                }
            }
        }
    }
    service_discovery();
    service_apc_roles();
    for (uint8_t index = 0; index < CFG_TUH_MIDI; ++index) {
        if (interfaces[index].busy) continue;
        const discovered_device *binding = mounted_binding(index);
        if (binding != NULL && !service_musical_out(index, binding)) service_apc_led_out(index);
    }
}
#else
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

#endif

#ifdef GRIDPICO_PICO_GENERIC_MIDI
static void service_native_epoch(void)
{
    if (native_epoch_current()) return;
    if (native_active_epoch != 0) {
        native_active_epoch = 0;
        native_event_state = NATIVE_EVENT_IDLE;
        native_out.kind = NATIVE_OUT_IDLE;
        native_led_refresh_at = 0;
        gridpico_pico_router_attach(&router, GRIDPICO_PICO_WEB, false);
    }

    /* Reopen only after the latest observed BUS_RESET was queued, the
     * queue drained and a configuration survived the reset. UNPLUGGED
     * only invalidates ownership. An old queued mount alone cannot reopen.
     */
    const uint32_t irq = save_and_disable_interrupts();
    const uint32_t epoch = native_bus_epoch;
    const bool configured = epoch != 0 && epoch != UINT32_MAX &&
        epoch == native_accepted_reset_epoch && epoch == native_mounted_epoch &&
        native_configuration != UINT32_MAX && !tud_task_event_ready() && tud_midi_mounted() &&
        !tud_suspended() && usbd_edpt_ready(DEVICE_ROOT, GRIDPICO_NATIVE_MIDI_IN_EP) &&
        native_endpoint_quiet();
    if (configured) {
        native_active_epoch = epoch;
        native_active_configuration = native_configuration;
        native_out_started = false;
    }
    restore_interrupts(irq);
    if (configured) gridpico_pico_router_attach(&router, GRIDPICO_PICO_WEB, true);
}

static bool submit_native_packet(const gridpico_pico_packet *packet, native_out_kind kind)
{
    native_out.packet = *packet;
    /* No MIDI class FIFO writes or refresh bypasses on this endpoint. The
     * same-core deadline and four-byte direct submission own one flight.
     */
    const uint32_t irq = save_and_disable_interrupts();
    bool accepted = false;
    if (native_epoch_current() && !tud_suspended() &&
        (native_out_started || !tud_task_event_ready()) && native_endpoint_quiet() &&
        usbd_edpt_claim(DEVICE_ROOT, GRIDPICO_NATIVE_MIDI_IN_EP)) {
        native_event_state = NATIVE_EVENT_ARMED;
        accepted = usbd_edpt_xfer(DEVICE_ROOT, GRIDPICO_NATIVE_MIDI_IN_EP, native_out.packet.bytes,
                                 GRIDPICO_PICO_PACKET_BYTES, false);
        if (accepted) {
            native_out.generation = router.generation[GRIDPICO_PICO_WEB];
            native_out.kind = kind;
            native_out_started = true;
        } else {
            native_event_state = NATIVE_EVENT_IDLE;
        }
    }
    restore_interrupts(irq);
    if (accepted) native_out.deadline = time_us_64() + TX_TIMEOUT_US;
    return accepted;
}

static void service_native_midi(void)
{
    service_native_epoch();
    if (!native_epoch_current() || !router.attached[GRIDPICO_PICO_WEB]) return;
    const uint64_t now = time_us_64();
    if (native_out.kind != NATIVE_OUT_IDLE && now >= native_out.deadline) {
        native_event_state = NATIVE_EVENT_IDLE;
        native_out.kind = NATIVE_OUT_IDLE;
        gridpico_pico_router_retire_native_output(&router);
    }
    if (tud_suspended()) return;
    gridpico_pico_packet packet;
    for (unsigned i = 0; i < RX_BATCH && tud_midi_packet_read(packet.bytes); ++i) {
        if (!native_epoch_current()) return;
        if (packet.bytes[0] >> 4 == 0 || router.native_output_ready) receive(GRIDPICO_PICO_WEB, &packet);
    }
    if (!router.native_output_ready || native_out.kind != NATIVE_OUT_IDLE) return;

    const gridpico_pico_packet *note = gridpico_pico_router_note(&router, GRIDPICO_PICO_WEB);
    if (note != NULL) {
        if (submit_native_packet(note, NATIVE_OUT_NOTE)) {
            add_trace('G', GRIDPICO_PICO_WEB, note->bytes, sizeof(note->bytes));
        }
        return;
    }
    const gridpico_pico_packet *led = gridpico_pico_router_led(&router, GRIDPICO_PICO_WEB);
    if (led != NULL) {
        if (submit_native_packet(led, NATIVE_OUT_PANEL)) gridpico_pico_router_led_sent(&router, GRIDPICO_PICO_WEB);
        return;
    }
    if (native_led_refresh_at == 0) {
        native_led_refresh_at = now + NATIVE_LED_REFRESH_US;
    } else if (now >= native_led_refresh_at) {
        native_led_refresh_at = now + NATIVE_LED_REFRESH_US;
        if (!tud_suspended() && usbd_edpt_ready(DEVICE_ROOT, GRIDPICO_NATIVE_MIDI_IN_EP)) {
            gridpico_pico_router_offer_led_page(&router, GRIDPICO_PICO_WEB);
        }
    }
}
#else
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
#endif

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

#ifdef GRIDPICO_RP2040_ONBOARD_STATUS
/* Keep the LED sample off the router's foreground dispatch stack frame. */
static void __attribute__((noinline)) service_onboard_status(void)
{
    static uint64_t next_sample;
    const uint64_t now = time_us_64();
    if (now < next_sample) return;
    next_sample = now + 10000;

#ifdef GRIDPICO_PICO_GENERIC_MIDI
    gridpico_onboard_facts facts = {.started = true};
    for (uint8_t index = 0; index < CFG_TUH_MIDI; ++index) {
        if (!observed_interface(index)) continue;
        facts.fault |= interfaces[index].faulted;
        facts.midi |= !interfaces[index].faulted && mounted_binding(index) != NULL;
    }
#else
    gridpico_onboard_facts facts = {
        .started = true,
        .midi = (apc_interface != NO_INTERFACE && tuh_midi_mounted(apc_interface)) ||
                (synth_interface != NO_INTERFACE && tuh_midi_mounted(synth_interface)),
        .recovery = apc_ambiguous || synth_ambiguous,
    };
    /* Only the selected APC and synth can submit OUT and latch a fault. The
       resolver drops their selections on fault, so inspect the live slots. */
    for (uint8_t i = 0; i < CFG_TUH_MIDI; ++i) {
        facts.fault |= interfaces[i].present && interfaces[i].faulted &&
                       reserved_startup_interface(&interfaces[i]) && tuh_midi_mounted(i);
    }
#endif
    /* Pinned TinyUSB allocates hub addresses after its ordinary device slots. */
    for (uint8_t address = CFG_TUH_DEVICE_MAX + 1; address <= CFG_TUH_DEVICE_MAX + CFG_TUH_HUB; ++address) {
        facts.hub |= tuh_mounted(address);
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
#ifdef GRIDPICO_RP2040_FLASH_ROUTER
    const uint64_t boot_deadline = time_us_64() + FLASH_START_DEADLINE_US;
#endif
#ifdef GRIDPICO_RP2040_ONBOARD_STATUS
    gridpico_onboard_init();
    gridpico_onboard_show(GRIDPICO_ONBOARD_STARTING);
#endif
#ifdef GRIDPICO_RP2040_FLASH_ROUTER
    if (!gridpico_flash_init_usb_serial()) enter_rom();
    bool startup_ready = false;
    bool cdc_ready_seen = false;
    uint32_t host_frame_at_start = 0;
#endif
    const tusb_rhport_init_t device = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    if (!tusb_init(DEVICE_ROOT, &device) || gridpico_pico_router_init(&router) != GRIDPICO_OK) {
#ifdef GRIDPICO_RP2040_ONBOARD_STATUS
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
#ifdef GRIDPICO_PICO_GENERIC_MIDI
        if (native_active_epoch != 0 && !native_epoch_current()) service_native_epoch();
#endif
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
#ifdef GRIDPICO_RP2040_FLASH_ROUTER
            if (!host_ready) {
#ifdef GRIDPICO_RP2040_ONBOARD_STATUS
                gridpico_onboard_show(GRIDPICO_ONBOARD_FAULT);
#endif
                enter_rom();
            }
            host_frame_at_start = hcd_frame_number(HOST_ROOT);
#endif
        }
        if (host_ready) {
            tuh_task_ext(0, false);
            service_host();
#ifdef GRIDPICO_PICO_GENERIC_MIDI
            service_automatic_policy();
#endif
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
#ifdef GRIDPICO_RP2040_ONBOARD_STATUS
        if (startup_ready) service_onboard_status();
#endif
        watchdog_update();
    }
}
