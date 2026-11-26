#define _POSIX_C_SOURCE 200809L

#include "app/local_router.h"

#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { MAX_LINE_BYTES = 544, MAX_FIELDS = 6, SERVICE_BATCH = 16 };

typedef struct {
    gridpico_connection connection;
    gridpico_patch_direction direction;
    unsigned limit;
    bool bound;
} reply;

static bool monotonic_ms(uint64_t *now)
{
    struct timespec sample;
    if (clock_gettime(CLOCK_MONOTONIC, &sample) != 0) {
        return false;
    }
    *now = (uint64_t)sample.tv_sec * 1000u + (uint64_t)sample.tv_nsec / 1000000u;
    return true;
}

static const char *result_line(gridpico_result result)
{
    switch (result) {
    case GRIDPICO_OK: return "ok";
    case GRIDPICO_INVALID: return "err INVALID";
    case GRIDPICO_CAPACITY: return "err CAPACITY";
    case GRIDPICO_UNAVAILABLE: return "err UNAVAILABLE";
    }
    return "err UNAVAILABLE";
}

static bool decimal(const char *text, uint64_t maximum, uint64_t *result)
{
    uint64_t value = 0;
    for (; *text != '\0'; ++text) {
        if (*text < '0' || *text > '9') {
            return false;
        }
        const unsigned digit = (unsigned)(*text - '0');
        if (value > (maximum - digit) / 10u) {
            return false;
        }
        value = value * 10u + digit;
    }
    *result = value;
    return value != 0;
}

static int hex_digit(unsigned char digit)
{
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    return -1;
}

static size_t decode_hex(const char *text, uint8_t *bytes, size_t capacity)
{
    const size_t length = strlen(text);
    if (length == 0 || length % 2u != 0 || length / 2u > capacity) {
        return 0;
    }
    for (size_t i = 0; i < length; i += 2u) {
        const int high = hex_digit((unsigned char)text[i]);
        const int low = hex_digit((unsigned char)text[i + 1u]);
        if (high < 0 || low < 0) {
            return 0;
        }
        bytes[i / 2u] = (uint8_t)(high * 16 + low);
    }
    return length / 2u;
}

static size_t split_fields(char *line, char *fields[MAX_FIELDS])
{
    size_t count = 0;
    char *field = line;
    for (;;) {
        if (*field == ' ' || *field == '\0' || count == MAX_FIELDS) {
            return 0;
        }
        fields[count++] = field;
        char *separator = strchr(field, ' ');
        if (separator == NULL) {
            return count;
        }
        *separator = '\0';
        field = separator + 1;
    }
}

static gridpico_result dispatch(local_runtime *runtime, char *line, reply *response)
{
    char *fields[MAX_FIELDS] = {0};
    const size_t count = split_fields(line, fields);
    if (count < 2) {
        return GRIDPICO_INVALID;
    }
    const char *command = fields[0];
    uint64_t generation;
    if (strncmp(command, "serial", sizeof("serial") - 1u) == 0) {
        if (!decimal(fields[1], UINT32_MAX, &generation)) {
            return GRIDPICO_INVALID;
        }
        if (count == 2 && strcmp(command, "serial-bind") == 0) {
            return local_runtime_serial_bind(runtime, (uint32_t)generation);
        }
        if (count == 2 && strcmp(command, "serial-detach") == 0) {
            return local_runtime_serial_detach(runtime, (uint32_t)generation);
        }
        if (count == 3 && strcmp(command, "serial") == 0) {
            uint8_t bytes[LOCAL_SERIAL_CHUNK];
            const size_t size = decode_hex(fields[2], bytes, sizeof(bytes));
            return local_runtime_serial(runtime, (uint32_t)generation, (const char *)bytes, size);
        }
        return GRIDPICO_INVALID;
    }

    uint64_t port;
    if (count < 3 || !decimal(fields[1], UINT16_MAX, &port) ||
        !decimal(fields[2], UINT32_MAX, &generation)) {
        return GRIDPICO_INVALID;
    }
    const gridpico_connection connection = {(gridpico_port_id)port, (uint32_t)generation};
    const bool managed_output_only = strcmp(command, "bind-managed-out") == 0;
    if (count == 3 && (strcmp(command, "bind") == 0 || managed_output_only)) {
        const gridpico_result result = managed_output_only ?
            local_runtime_bind_managed_out(runtime, connection, &response->direction,
                                           &response->limit) :
            local_runtime_bind(runtime, connection, &response->direction, &response->limit);
        response->bound = result == GRIDPICO_OK;
        response->connection = connection;
        return result;
    }
    local_port_state port_state;
    if (local_runtime_port(runtime, connection.endpoint, &port_state) == GRIDPICO_OK &&
        port_state.adapter_owned) {
        return GRIDPICO_UNAVAILABLE;
    }
    if (count == 3 && strcmp(command, "detach") == 0) {
        return local_runtime_detach(runtime, connection);
    }
    if ((count == 4 || count == 6) && strcmp(command, "midi") == 0) {
        const char *hex = fields[3];
        gridpico_revision revision = 0;
        if (count == 6) {
            hex = fields[5];
            if (!runtime->forwarding_enabled || strcmp(fields[3], "ordinary") != 0 ||
                fields[4][0] == '0' || !decimal(fields[4], UINT64_MAX, &revision) ||
                strspn(hex, "0123456789ABCDEF") != 2u * GRIDPICO_LOCAL_MESSAGE_BYTES) return GRIDPICO_INVALID;
        }
        gridpico_local_raw_ingress input = {.source = connection};
        if (decode_hex(hex, input.bytes, sizeof(input.bytes)) == sizeof(input.bytes)) {
            return local_runtime_midi_at_revision(runtime, &input, revision);
        }
    }
    return GRIDPICO_INVALID;
}

static bool write_hex(const char *bytes, size_t count)
{
    const char digits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < count; ++i) {
        const unsigned byte = (unsigned char)bytes[i];
        if (putchar(digits[byte >> 4]) == EOF || putchar(digits[byte & 0x0fu]) == EOF) {
            return false;
        }
    }
    return true;
}

static bool write_serial_range(const local_runtime *runtime, size_t sent, size_t end)
{
    for (; sent < end;) {
        const size_t remaining = end - sent;
        const size_t count = remaining < LOCAL_SERIAL_CHUNK ? remaining : LOCAL_SERIAL_CHUNK;
        if (printf("serial %" PRIu32 " ", runtime->serial.generation) < 0 ||
            !write_hex(runtime->serial_bytes + sent, count) || putchar('\n') == EOF) return false;
        sent += count;
    }
    return true;
}

static bool write_records(const local_runtime *runtime)
{
    size_t serial_offset = 0;
    for (size_t i = 0; i < runtime->forwarding_barrier_count; ++i) {
        const local_forwarding_barrier *barrier = &runtime->forwarding_barriers[i];
        if (!runtime->serial.fault &&
            !write_serial_range(runtime, serial_offset, barrier->serial_offset)) return false;
        if (printf("forwarding-state %" PRIu64 " %s\n", barrier->revision,
                   barrier->running ? "running" : "paused") < 0) return false;
        serial_offset = barrier->serial_offset;
    }
    for (size_t i = 0; i < runtime->output_count; ++i) {
        const local_output *row = &runtime->output[i];
        const unsigned port = row->destination.endpoint;
        const uint32_t generation = row->destination.generation;
        if (row->fault) {
            if (printf("fault midi %u %" PRIu32 "\n", port, generation) < 0) {
                return false;
            }
        } else {
            local_port_state state;
            if (local_runtime_port(runtime, row->destination.endpoint, &state) != GRIDPICO_OK ||
                !state.attached || state.generation != generation) continue;
            if (row->forwarding_revision != 0 && (!runtime->forwarding_running ||
                row->forwarding_revision != runtime->forwarding_revision)) continue;
            if (printf("midi %u %" PRIu32 " ", port, generation) < 0) return false;
            if (runtime->forwarding_enabled) {
                const int prefix = row->forwarding_revision != 0
                    ? printf("ordinary %" PRIu64 " ", row->forwarding_revision) : printf("control - ");
                if (prefix < 0) return false;
            }
            if (!write_hex((const char *)row->bytes, sizeof(row->bytes)) || putchar('\n') == EOF) return false;
        }
    }
    if (runtime->serial.fault) {
        return printf("fault serial %" PRIu32 "\n", runtime->serial.generation) >= 0;
    }
    return write_serial_range(runtime, serial_offset, runtime->serial_count);
}

static bool write_event(const local_runtime *runtime)
{
    return puts("event") != EOF && write_records(runtime) && puts("end") != EOF &&
           fflush(stdout) == 0;
}

static bool expire(local_runtime *runtime, uint64_t now)
{
    if (runtime->raw_deadline_ms == 0 || now < runtime->raw_deadline_ms) {
        return true;
    }
    local_runtime_begin(runtime, now);
    if (local_runtime_finish(runtime, local_runtime_expire(runtime)) != GRIDPICO_OK) {
        fputs("Local router could not restore feedback after Raw expiry\n", stderr);
    }
    return write_event(runtime);
}

static bool handle_line(local_runtime *runtime, char *line, bool malformed)
{
    uint64_t now;
    if (!monotonic_ms(&now) || !expire(runtime, now)) {
        return false;
    }
    local_runtime_begin(runtime, now);
    reply response = {0};
    const gridpico_result dispatched = malformed ? GRIDPICO_INVALID
                                                  : dispatch(runtime, line, &response);
    const gridpico_result result = local_runtime_finish(runtime, dispatched);
    if (response.bound && result == GRIDPICO_OK) {
        const char *classification = "";
        if (runtime->forwarding_enabled) {
            local_port_state state;
            if (local_runtime_port(runtime, response.connection.endpoint, &state) != GRIDPICO_OK) return false;
            classification = state.managed ? " managed" : " control";
        }
        if (printf("bound %u %" PRIu32 " %s %u%s\n", (unsigned)response.connection.endpoint,
                   response.connection.generation,
                   response.direction == GRIDPICO_PATCH_INPUT ? "in" : "out",
                   response.limit, classification) < 0) return false;
    }
    return write_records(runtime) && puts(result_line(result)) != EOF && fflush(stdout) == 0;
}

static bool service_adapter(local_runtime *runtime, const local_router_service *service, uint64_t now)
{
    for (size_t i = 0; service != NULL && i < SERVICE_BATCH; ++i) {
        bool handled = false;
        local_runtime_begin(runtime, now);
        const gridpico_result result = service->next(service->context, runtime, &handled);
        if (!handled) {
            return result == GRIDPICO_OK;
        }
        local_runtime_finish(runtime, result);
        if ((runtime->output_count != 0 || runtime->serial_count != 0 ||
             runtime->serial.fault) && !write_event(runtime)) {
            return false;
        }
    }
    return true;
}

static bool serve(local_runtime *runtime, const local_router_service *service)
{
    char line[MAX_LINE_BYTES + 1];
    size_t position = 0;
    bool malformed = false;
    for (;;) {
        uint64_t now;
        if (!monotonic_ms(&now) || !expire(runtime, now) ||
            !service_adapter(runtime, service, now)) {
            return false;
        }
        int wait = runtime->raw_deadline_ms == 0 ? -1 :
            (int)(runtime->raw_deadline_ms - now);
        if (service != NULL && (wait < 0 || wait > service->interval_ms)) {
            wait = service->interval_ms;
        }
        struct pollfd input = {.fd = STDIN_FILENO, .events = POLLIN};
        const int ready = poll(&input, 1, wait);
        if (ready == 0 || (ready < 0 && errno == EINTR)) {
            continue;
        }
        if (ready < 0) {
            return false;
        }
        unsigned char bytes[1024];
        const ssize_t size = read(STDIN_FILENO, bytes, sizeof(bytes));
        if (size < 0 && errno == EINTR) {
            continue;
        }
        if (size <= 0) {
            return size == 0;
        }
        for (ssize_t i = 0; i < size; ++i) {
            const unsigned char byte = bytes[i];
            if (byte == '\n') {
                line[position] = '\0';
                if (!handle_line(runtime, line, malformed)) {
                    return false;
                }
                position = 0;
                malformed = false;
            } else if (byte < 0x20 || byte > 0x7e || position == MAX_LINE_BYTES) {
                malformed = true;
            } else if (!malformed) {
                line[position++] = (char)byte;
            }
        }
    }
}

static bool write_ui_roles(const local_runtime *runtime)
{
    if (fputs("ui-roles", stdout) == EOF) return false;
    for (local_port_role role = LOCAL_ROLE_KEYBED; role < LOCAL_ROLE_COUNT; ++role) {
        const gridpico_port_id id = local_profile_role(runtime->profile, role);
        local_port_state state;
        if (local_runtime_port(runtime, id, &state) != GRIDPICO_OK || !state.socket ||
            state.adapter_owned || state.generation == UINT32_MAX) {
            continue;
        }
        const char *direction = state.direction == GRIDPICO_PATCH_INPUT ? "in" : "out";
        if (printf(" %s %u %s", local_profile_role_token(role), (unsigned)id, direction) < 0) {
            return false;
        }
    }
    return putchar('\n') != EOF;
}

int local_router_run(local_runtime *runtime, const local_router_service *service, bool ui_descriptor)
{
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR || (ui_descriptor && !write_ui_roles(runtime))) {
        return EXIT_FAILURE;
    }
    const int ready = runtime->forwarding_enabled || runtime->configured
        ? printf("ready %zu%s\n", runtime->profile->limits.midi_peer_limit,
                 runtime->forwarding_enabled ? " forwarding-v1" : "") : puts("ready");
    const bool complete = ready >= 0 && fflush(stdout) == 0 &&
        (runtime->output_count == 0 || write_event(runtime)) && serve(runtime, service);
    local_runtime_begin(runtime, 0);
    const gridpico_result retired = local_runtime_retire(runtime);
    const bool flushed = complete && (runtime->output_count == 0 || write_event(runtime));
    if (!flushed || retired != GRIDPICO_OK) {
        fputs("Local router stopped with a pipe or detach error\n", stderr);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

#ifndef GRIDPICO_LOCAL_ROUTER_RUNNER_ONLY
int main(int argc, char **argv)
{
    bool web_ui = false;
    bool recorder = false;
    bool ui_descriptor = false;
    const char *path = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!web_ui && strcmp(argv[i], "--web-ui") == 0) {
            web_ui = true;
        } else if (!recorder && strcmp(argv[i], "--recorder") == 0) {
            recorder = true;
        } else if (!ui_descriptor && strcmp(argv[i], "--ui-descriptor") == 0) {
            ui_descriptor = true;
        } else if (path == NULL && i + 1 < argc && strcmp(argv[i], "--profile") == 0 &&
                   strncmp(argv[i + 1], "--", 2) != 0) {
            path = argv[++i];
        } else {
            fputs("Usage: gridpico_local_router [--web-ui] [--profile FILE] [--recorder] "
                  "[--ui-descriptor]\n", stderr);
            return EXIT_FAILURE;
        }
    }
    local_router_profile profile;
    if (path != NULL) {
        if (!local_profile_load(path, &profile)) return EXIT_FAILURE;
        if (web_ui && !local_profile_supports_web_ui(&profile)) {
            fputs("Router profile must offer the six starter socket roles at ports 1–6 for --web-ui\n",
                  stderr);
            return EXIT_FAILURE;
        }
    }
    local_runtime runtime;
    if (local_runtime_init_profile(&runtime, path != NULL ? &profile : NULL) != GRIDPICO_OK) {
        fputs("Local router could not initialize the production graph\n", stderr);
        return EXIT_FAILURE;
    }
    if (web_ui && local_runtime_enable_web_ui_routes(&runtime) != GRIDPICO_OK) {
        fputs("Local router could not configure the Web UI starter routes\n", stderr);
        return EXIT_FAILURE;
    }
    if (recorder && local_runtime_enable_recorder_route(&runtime) != GRIDPICO_OK) {
        fputs("Recorder requires a profile with a usable managed socket route 1 to 12\n", stderr);
        return EXIT_FAILURE;
    }
    local_runtime_enable_desktop_note_transform(&runtime);
    runtime.forwarding_enabled = ui_descriptor && !recorder;
    return local_router_run(&runtime, NULL, ui_descriptor);
}
#endif
