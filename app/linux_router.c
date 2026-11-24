#define _POSIX_C_SOURCE 200809L

#include "api/linux_midi.h"
#include "app/local_router.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    CONFIG_BYTES = 4096, ALSA_ADDRESSES = 65536, SERVICE_INTERVAL_MS = 5,
    RESCAN_EVENT_BUDGET = 2 * GRIDPICO_LOCAL_PORTS,
};

typedef struct {
    gridpico_port_id port;
    gridpico_patch_direction direction;
    gridpico_linux_midi_address address;
    gridpico_linux_usb_selector usb;
    bool verified_usb;
} selection;

typedef struct {
    selection rows[GRIDPICO_LOCAL_PORTS];
    size_t count;
} selection_list;

typedef struct {
    gridpico_connection connection;
    gridpico_linux_midi_address address;
} attachment;

typedef struct {
    gridpico_linux_midi *midi;
    const char *config;
    selection_list selected;
    attachment active[GRIDPICO_LOCAL_PORTS];
    bool rescan_pending;
    unsigned rescan_events_left;
} linux_router;

static volatile sig_atomic_t reload_requested;

static void request_reload(int signal_number)
{
    (void)signal_number;
    reload_requested = 1;
}

static bool same_address(gridpico_linux_midi_address a, gridpico_linux_midi_address b)
{
    return a.client == b.client && a.port == b.port;
}

static bool same_usb(const gridpico_linux_usb_selector *a, const gridpico_linux_usb_selector *b)
{
    return a->vendor_id == b->vendor_id && a->product_id == b->product_id &&
           a->interface_number == b->interface_number && a->serial_size == b->serial_size &&
           memcmp(a->serial, b->serial, a->serial_size) == 0;
}

static const char *direction_name(gridpico_patch_direction direction)
{
    return direction == GRIDPICO_PATCH_INPUT ? "in" : "out";
}

static size_t select_index(const selection_list *list, gridpico_port_id port)
{
    for (size_t i = 0; i < list->count; ++i) {
        if (list->rows[i].port == port) {
            return i;
        }
    }
    return list->count;
}

static bool number(char **cursor, unsigned long maximum, unsigned long *value)
{
    if (**cursor < '0' || **cursor > '9') {
        return false;
    }
    errno = 0;
    *value = strtoul(*cursor, cursor, 10);
    return errno == 0 && *value <= maximum;
}

static char *skip_space(char *text)
{
    while (isspace((unsigned char)*text)) {
        ++text;
    }
    return text;
}

static int hex_digit(unsigned char digit)
{
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    return -1;
}

static bool hex_field(char **cursor, unsigned digits, uint16_t *out)
{
    unsigned value = 0;
    for (unsigned i = 0; i < digits; ++i) {
        const int digit = hex_digit((unsigned char)**cursor);
        if (digit < 0) return false;
        value = value * 16u + (unsigned)digit;
        ++*cursor;
    }
    *out = (uint16_t)value;
    return true;
}

static bool parse_usb(char **cursor, gridpico_linux_usb_selector *selector)
{
    if (!hex_field(cursor, 4, &selector->vendor_id) || **cursor != ':') return false;
    ++*cursor;
    if (!hex_field(cursor, 4, &selector->product_id) || !isspace((unsigned char)**cursor)) {
        return false;
    }
    *cursor = skip_space(*cursor);
    while (!isspace((unsigned char)**cursor)) {
        uint16_t byte;
        if (selector->serial_size == GRIDPICO_LINUX_USB_SERIAL_BYTES ||
            !hex_field(cursor, 2, &byte)) return false;
        selector->serial[selector->serial_size++] = (uint8_t)byte;
    }
    *cursor = skip_space(*cursor);
    uint16_t interface;
    if (selector->serial_size == 0 || !hex_field(cursor, 2, &interface)) return false;
    selector->interface_number = (uint8_t)interface;
    return true;
}

static bool add_selection(const local_runtime *runtime, selection_list *list, char *line)
{
    char *cursor = skip_space(line);
    if (*cursor == '\0' || *cursor == '#') {
        return true;
    }
    unsigned long id;
    if (!number(&cursor, UINT16_MAX, &id) || !isspace((unsigned char)*cursor)) {
        return false;
    }
    cursor = skip_space(cursor);
    selection row = {.port = (gridpico_port_id)id};
    if (strncmp(cursor, "usb", 3) == 0 && isspace((unsigned char)cursor[3])) {
        row.verified_usb = true;
        cursor = skip_space(cursor + 3);
        if (!parse_usb(&cursor, &row.usb)) return false;
    } else {
        unsigned long client, port;
        if (!number(&cursor, UINT8_MAX, &client) || *cursor++ != ':' ||
            !number(&cursor, UINT8_MAX, &port)) return false;
        row.address = (gridpico_linux_midi_address){(uint8_t)client, (uint8_t)port};
    }
    cursor = skip_space(cursor);
    local_port_state state;
    if ((*cursor != '\0' && *cursor != '#') || list->count == GRIDPICO_LOCAL_PORTS ||
        local_runtime_port(runtime, row.port, &state) != GRIDPICO_OK ||
        select_index(list, row.port) != list->count) {
        return false;
    }
    row.direction = state.direction;
    for (size_t i = 0; i < list->count; ++i) {
        const selection *other = &list->rows[i];
        if (other->direction == row.direction && other->verified_usb == row.verified_usb) {
            if (row.verified_usb ? same_usb(&other->usb, &row.usb)
                                 : same_address(other->address, row.address)) return false;
        }
    }
    list->rows[list->count++] = row;
    return true;
}

static bool load_config(const char *path, const local_runtime *runtime, selection_list *list)
{
    char content[CONFIG_BYTES + 1];
    const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > CONFIG_BYTES) {
        if (fd >= 0) {
            close(fd);
        }
        fprintf(stderr, "ALSA bindings need a readable regular file of at most %d bytes: %s\n",
                CONFIG_BYTES, path);
        return false;
    }
    size_t used = 0;
    ssize_t size = 0;
    while (used < sizeof(content)) {
        size = read(fd, content + used, sizeof(content) - used);
        if (size < 0 && errno == EINTR) {
            continue;
        }
        if (size <= 0) {
            break;
        }
        used += (size_t)size;
    }
    close(fd);
    if (size < 0 || used == sizeof(content) || memchr(content, '\0', used) != NULL) {
        fprintf(stderr, "Cannot read ALSA binding rows from %s\n", path);
        return false;
    }
    content[used] = '\0';
    char *line = content;
    for (size_t line_no = 1; line != NULL; ++line_no) {
        char *next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        if (!add_selection(runtime, list, line)) {
            fprintf(stderr, "%s:%zu: expected a unique logical port and an ALSA address or USB selector\n",
                    path, line_no);
            return false;
        }
        line = next;
    }
    return true;
}

static bool read_ports(gridpico_linux_midi *midi, gridpico_linux_midi_port **ports, size_t *count)
{
    /* Inventory can change between the count and the copy. Retry one growth. */
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        if (gridpico_linux_midi_list(midi, NULL, 0, count) != GRIDPICO_OK ||
            *count > ALSA_ADDRESSES) {
            break;
        }
        if (*count == 0) {
            *ports = NULL;
            return true;
        }
        gridpico_linux_midi_port *rows = calloc(*count, sizeof(*rows));
        if (rows == NULL) {
            break;
        }
        const gridpico_result result = gridpico_linux_midi_list(midi, rows, *count, count);
        if (result == GRIDPICO_OK) {
            *ports = rows;
            return true;
        }
        free(rows);
        if (result != GRIDPICO_CAPACITY) {
            break;
        }
    }
    fputs("Cannot enumerate ALSA sequencer ports\n", stderr);
    return false;
}

static bool verify_selections(gridpico_linux_midi *midi, const selection_list *list)
{
    gridpico_linux_midi_port *ports = NULL;
    size_t count = 0;
    bool read = false;
    bool verified = true;
    for (size_t i = 0; i < list->count; ++i) {
        const selection *row = &list->rows[i];
        if (row->verified_usb) continue;
        if (!read) {
            if (!read_ports(midi, &ports, &count)) return false;
            read = true;
        }
        size_t j = 0;
        for (; j < count; ++j) {
            const bool allowed = row->direction == GRIDPICO_PATCH_INPUT
                ? ports[j].can_read : ports[j].can_write;
            if (allowed && same_address(row->address, ports[j].address)) {
                break;
            }
        }
        if (j == count) {
            fprintf(stderr, "ALSA %u:%u cannot serve logical port %u (%s)\n",
                    (unsigned)row->address.client, (unsigned)row->address.port,
                    (unsigned)row->port, direction_name(row->direction));
            verified = false;
        }
    }
    free(ports);
    return verified;
}

static gridpico_result send_midi(void *context, gridpico_connection connection,
                                const uint8_t bytes[GRIDPICO_LOCAL_MESSAGE_BYTES])
{
    linux_router *router = context;
    const gridpico_result result = gridpico_linux_midi_send(router->midi, connection, bytes);
    if (result != GRIDPICO_OK) {
        fprintf(stderr, "ALSA send failed for logical port %u generation %" PRIu32 " (%d)\n",
                (unsigned)connection.endpoint, connection.generation, result);
    }
    return result;
}

static void retire_midi(void *context, gridpico_connection connection)
{
    linux_router *router = context;
    const size_t i = select_index(&router->selected, connection.endpoint);
    if (i == router->selected.count ||
        router->active[i].connection.generation != connection.generation) {
        return;
    }
    const gridpico_result result = gridpico_linux_midi_unbind(router->midi, connection);
    router->active[i] = (attachment){0};
    fprintf(stderr, "ALSA retired logical port %u generation %" PRIu32 " (%d)\n",
            (unsigned)connection.endpoint, connection.generation, result);
}

static gridpico_result attach(linux_router *router, local_runtime *runtime, size_t i)
{
    const selection *row = &router->selected.rows[i];
    local_port_state state;
    if (local_runtime_port(runtime, row->port, &state) != GRIDPICO_OK ||
        !state.adapter_owned || state.attached || state.generation == UINT32_MAX) {
        return GRIDPICO_UNAVAILABLE;
    }
    if (row->verified_usb && state.direction == GRIDPICO_PATCH_OUTPUT) {
        fprintf(stderr, "Verified USB output logical port %u is reserved and remains pending\n",
                (unsigned)row->port);
        return GRIDPICO_UNAVAILABLE;
    }
    const gridpico_connection connection = {row->port, state.generation + 1u};
    gridpico_linux_midi_address address = row->address;
    gridpico_result result = row->verified_usb
        ? gridpico_linux_midi_bind_usb(router->midi, &row->usb, state.direction, connection, &address)
        : gridpico_linux_midi_bind(router->midi, address, state.direction, connection);
    if (row->verified_usb && result != GRIDPICO_OK) {
        fprintf(stderr, "Verified USB logical port %u (%s) is pending (%d)\n",
                (unsigned)row->port, direction_name(state.direction), result);
        return result;
    }
    if (result == GRIDPICO_OK) {
        router->active[i] = (attachment){connection, address};
        const local_midi_adapter adapter = {
            .context = router, .retire = retire_midi,
            .send = state.direction == GRIDPICO_PATCH_OUTPUT ? send_midi : NULL,
        };
        result = local_runtime_bind_adapter(runtime, connection, adapter);
        if (result != GRIDPICO_OK) {
            retire_midi(router, connection);
        }
    }
    fprintf(stderr, "ALSA %s logical port %u (%s) at %u:%u generation %" PRIu32 " (%d)\n",
            result == GRIDPICO_OK ? "attached" : "could not attach",
            (unsigned)row->port, direction_name(state.direction),
            (unsigned)address.client, (unsigned)address.port,
            connection.generation, result);
    return result;
}

static gridpico_result attach_missing(linux_router *router, local_runtime *runtime)
{
    gridpico_result result = GRIDPICO_OK;
    const gridpico_patch_direction order[] = {GRIDPICO_PATCH_OUTPUT, GRIDPICO_PATCH_INPUT};
    for (size_t direction = 0; direction < 2; ++direction) {
        for (size_t i = 0; i < router->selected.count; ++i) {
            if (router->selected.rows[i].direction == order[direction] &&
                router->active[i].connection.generation == 0) {
                const gridpico_result attached = attach(router, runtime, i);
                if (result == GRIDPICO_OK && !router->selected.rows[i].verified_usb) {
                    result = attached;
                }
            }
        }
    }
    return result;
}

static gridpico_result rebind(linux_router *router, local_runtime *runtime)
{
    selection_list desired = {0};
    if (!load_config(router->config, runtime, &desired)) {
        return GRIDPICO_UNAVAILABLE;
    }
    if (desired.count != router->selected.count) {
        fputs("Changing logical ALSA port ownership requires a new router process\n", stderr);
        return GRIDPICO_INVALID;
    }
    selection_list ordered = router->selected;
    selection_list changed = {0};
    for (size_t i = 0; i < ordered.count; ++i) {
        const size_t j = select_index(&desired, ordered.rows[i].port);
        if (j == desired.count) {
            fputs("Changing logical ALSA port ownership requires a new router process\n", stderr);
            return GRIDPICO_INVALID;
        }
        ordered.rows[i] = desired.rows[j];
        const selection *previous = &router->selected.rows[i];
        const selection *replacement = &ordered.rows[i];
        if (previous->verified_usb != replacement->verified_usb ||
            (previous->verified_usb && !same_usb(&previous->usb, &replacement->usb))) {
            fputs("Changing an ALSA selector kind or USB identity requires a new router process\n", stderr);
            return GRIDPICO_INVALID;
        }
        if (!previous->verified_usb && !same_address(previous->address, replacement->address)) {
            changed.rows[changed.count++] = ordered.rows[i];
        }
    }
    if (!verify_selections(router->midi, &changed)) {
        return GRIDPICO_UNAVAILABLE;
    }
    gridpico_result result = GRIDPICO_OK;
    const gridpico_patch_direction order[] = {GRIDPICO_PATCH_INPUT, GRIDPICO_PATCH_OUTPUT};
    for (size_t direction = 0; direction < 2; ++direction) {
        for (size_t i = 0; i < ordered.count; ++i) {
            const attachment active = router->active[i];
            const selection *row = &ordered.rows[i];
            if (row->direction != order[direction] || active.connection.generation == 0) continue;
            const bool retire = row->verified_usb
                ? gridpico_linux_midi_check_usb_binding(router->midi, &row->usb,
                                                       active.connection) != GRIDPICO_OK
                : !same_address(active.address, row->address);
            if (retire) {
                const gridpico_result detached = local_runtime_detach(runtime, active.connection);
                if (result == GRIDPICO_OK) {
                    result = detached;
                }
            }
        }
    }
    router->selected = ordered;
    const gridpico_result attached = attach_missing(router, runtime);
    return result == GRIDPICO_OK ? attached : result;
}

static gridpico_result next_event(void *context, local_runtime *runtime, bool *handled)
{
    linux_router *router = context;
    if (reload_requested) {
        reload_requested = 0;
        if (!router->rescan_pending) router->rescan_events_left = RESCAN_EVENT_BUDGET;
        router->rescan_pending = true;
    }
    gridpico_linux_midi_event event;
    if (!router->rescan_pending || router->rescan_events_left != 0) {
        const gridpico_result result = gridpico_linux_midi_next(router->midi, &event, handled);
        if (result != GRIDPICO_OK || (!router->rescan_pending && !*handled)) return result;
    }
    /* Drain the adapter's observed events first, with a bound under live input. */
    if (router->rescan_pending && (!*handled || router->rescan_events_left == 0)) {
        router->rescan_pending = false;
        *handled = true;
        return rebind(router, runtime);
    }
    if (router->rescan_pending) --router->rescan_events_left;
    const size_t i = select_index(&router->selected, event.connection.endpoint);
    if (i == router->selected.count ||
        router->active[i].connection.generation != event.connection.generation) {
        return GRIDPICO_OK;
    }
    if (event.kind == GRIDPICO_LINUX_MIDI_RECEIVED) {
        gridpico_local_raw_ingress input = {.source = event.connection};
        memcpy(input.bytes, event.bytes, sizeof(input.bytes));
        return local_runtime_midi(runtime, &input);
    }
    fprintf(stderr, "ALSA lifecycle event %d for logical port %u generation %" PRIu32 "\n",
            event.kind, (unsigned)event.connection.endpoint, event.connection.generation);
    return local_runtime_detach(runtime, event.connection);
}

static int list_ports(gridpico_linux_midi *midi)
{
    gridpico_linux_midi_port *ports;
    size_t count;
    if (!read_ports(midi, &ports, &count)) {
        return EXIT_FAILURE;
    }
    puts("CLIENT:PORT  READ  WRITE  NAME");
    for (size_t i = 0; i < count; ++i) {
        const gridpico_linux_midi_port *row = &ports[i];
        printf("%u:%u  %s  %s  %s\n", (unsigned)row->address.client, (unsigned)row->address.port,
               row->can_read ? "yes" : "-", row->can_write ? "yes" : "-", row->name);
    }
    free(ports);
    return fflush(stdout) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

static void usage(FILE *out)
{
    fputs("Usage: gridpico_linux_router [--list-alsa | --help]\n"
          "Set GRIDPICO_ALSA_BINDINGS_FILE to a file with LOGICAL_PORT CLIENT:PORT rows.\n"
          "For verified USB input use LOGICAL_PORT usb VID:PID SERIAL_HEX INTERFACE.\n"
          "Use four hex digits for VID and PID, 2-128 even serial digits, and two for interface.\n"
          "A USB output selector reserves its port and remains pending. Outputs can use explicit CLIENT:PORT.\n"
          "Use readable ALSA ports for router inputs and writable ports for outputs.\n"
          "Configure routes through the existing serial CLI. Unmapped ports use the local host.\n"
          "SIGHUP rechecks explicit address and verified USB input bindings. Unmatched inputs stay pending.\n"
          "Changing logical ALSA port ownership, selector kind or USB identity requires a new process.\n", out);
}

static int run(linux_router *router)
{
    local_runtime runtime;
    if (local_runtime_init(&runtime) != GRIDPICO_OK ||
        !load_config(router->config, &runtime, &router->selected) ||
        !verify_selections(router->midi, &router->selected)) {
        return EXIT_FAILURE;
    }
    struct sigaction action = {.sa_handler = request_reload};
    if (sigemptyset(&action.sa_mask) == -1 || sigaction(SIGHUP, &action, NULL) == -1) {
        perror("Cannot handle ALSA rebind signal");
        return EXIT_FAILURE;
    }
    for (size_t i = 0; i < router->selected.count; ++i) {
        if (local_runtime_reserve_adapter(&runtime, router->selected.rows[i].port) != GRIDPICO_OK) {
            return EXIT_FAILURE;
        }
    }
    local_runtime_begin(&runtime, 0);
    if (local_runtime_finish(&runtime, attach_missing(router, &runtime)) != GRIDPICO_OK) {
        local_runtime_begin(&runtime, 0);
        local_runtime_retire(&runtime);
        return EXIT_FAILURE;
    }
    fprintf(stderr, "Linux router PID %ld; SIGHUP rechecks ALSA bindings\n", (long)getpid());
    const local_router_service service = {router, next_event, SERVICE_INTERVAL_MS};
    return local_router_run(&runtime, &service, false);
}

int main(int argc, char **argv)
{
    const bool list = argc == 2 && strcmp(argv[1], "--list-alsa") == 0;
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(stdout);
        return EXIT_SUCCESS;
    }
    if (argc != 1 && !list) {
        usage(stderr);
        return EXIT_FAILURE;
    }
    linux_router router = {.config = getenv("GRIDPICO_ALSA_BINDINGS_FILE")};
    if (!list && (router.config == NULL || *router.config == '\0')) {
        usage(stderr);
        return EXIT_FAILURE;
    }
    if (gridpico_linux_midi_open(&router.midi) != GRIDPICO_OK) {
        fputs("Cannot open ALSA sequencer /dev/snd/seq\n", stderr);
        return EXIT_FAILURE;
    }
    const int result = list ? list_ports(router.midi) : run(&router);
    gridpico_linux_midi_close(router.midi);
    return result;
}
