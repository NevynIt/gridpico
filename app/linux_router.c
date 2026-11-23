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

enum { CONFIG_BYTES = 4096, ALSA_ADDRESSES = 65536, SERVICE_INTERVAL_MS = 5 };

typedef struct {
    gridpico_port_id port;
    gridpico_patch_direction direction;
    gridpico_linux_midi_address address;
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

static bool add_selection(const local_runtime *runtime, selection_list *list, char *line)
{
    char *cursor = skip_space(line);
    if (*cursor == '\0' || *cursor == '#') {
        return true;
    }
    unsigned long id, client, port;
    if (!number(&cursor, UINT16_MAX, &id) || !isspace((unsigned char)*cursor)) {
        return false;
    }
    cursor = skip_space(cursor);
    if (!number(&cursor, UINT8_MAX, &client) || *cursor++ != ':' ||
        !number(&cursor, UINT8_MAX, &port)) {
        return false;
    }
    cursor = skip_space(cursor);
    const gridpico_port_id logical = (gridpico_port_id)id;
    local_port_state state;
    if ((*cursor != '\0' && *cursor != '#') || list->count == GRIDPICO_LOCAL_PORTS ||
        local_runtime_port(runtime, logical, &state) != GRIDPICO_OK ||
        select_index(list, logical) != list->count) {
        return false;
    }
    const gridpico_linux_midi_address address = {(uint8_t)client, (uint8_t)port};
    for (size_t i = 0; i < list->count; ++i) {
        if (list->rows[i].direction == state.direction &&
            same_address(list->rows[i].address, address)) {
            return false;
        }
    }
    list->rows[list->count++] = (selection){logical, state.direction, address};
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
            fprintf(stderr, "%s:%zu: expected a unique logical port and ALSA CLIENT:PORT\n",
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
    gridpico_linux_midi_port *ports;
    size_t count;
    if (list->count == 0) {
        return true;
    }
    if (!read_ports(midi, &ports, &count)) {
        return false;
    }
    bool verified = true;
    for (size_t i = 0; i < list->count; ++i) {
        const selection *row = &list->rows[i];
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
    const gridpico_connection connection = {row->port, state.generation + 1u};
    gridpico_result result = gridpico_linux_midi_bind(router->midi, row->address,
                                                    state.direction, connection);
    if (result == GRIDPICO_OK) {
        router->active[i] = (attachment){connection, row->address};
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
            (unsigned)row->address.client, (unsigned)row->address.port,
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
                if (result == GRIDPICO_OK) {
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
        if (!same_address(router->selected.rows[i].address, ordered.rows[i].address)) {
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
            if (ordered.rows[i].direction == order[direction] && active.connection.generation != 0 &&
                !same_address(active.address, ordered.rows[i].address)) {
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
        *handled = true;
        return rebind(router, runtime);
    }
    gridpico_linux_midi_event event;
    const gridpico_result result = gridpico_linux_midi_next(router->midi, &event, handled);
    if (result != GRIDPICO_OK || !*handled) {
        return result;
    }
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
          "Use readable ALSA ports for router inputs and writable ports for outputs.\n"
          "Configure routes through the existing serial CLI. Unmapped ports use the local host.\n"
          "SIGHUP rechecks selections and attaches changed or retired addresses.\n"
          "Changing which logical ports use ALSA requires a new router process.\n", out);
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
    return local_router_run(&runtime, &service);
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
