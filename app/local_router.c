#define _POSIX_C_SOURCE 200809L

#include "app/local_router.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { MAX_LINE_BYTES = 544, MAX_FIELDS = 7, SERVICE_BATCH = 16, FM_RESERVATION_MS = 5000 };

typedef struct {
    uint64_t rid;
    uint64_t sid;
    local_fm_lease lease;
} fm_pipe_owner;

typedef struct {
    bool prepared;
    bool probe_open;
    uint64_t last_rid;
    uint64_t last_sid;
    /* Row addresses are owner references. Graph compaction never moves them. */
    fm_pipe_owner owners[GRIDPICO_PATCH_MAX_DEVICES];
} fm_pipe;

typedef enum {
    FM_REPLY_NONE,
    FM_REPLY_READY,
    FM_REPLY_RESERVED,
    FM_REPLY_PUBLISHED,
} fm_reply_kind;

typedef struct {
    gridpico_connection connection;
    gridpico_patch_direction direction;
    unsigned limit;
    bool bound;
    bool apc_ready;
    fm_reply_kind fm_kind;
    fm_pipe_owner *fm_owner;
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

static bool canonical_decimal(const char *text, uint64_t maximum, uint64_t *out)
{
    return text[0] != '0' && decimal(text, maximum, out);
}

static void release_unused_fm_owners(local_runtime *runtime, fm_pipe *fm)
{
    for (size_t i = 0; i < GRIDPICO_PATCH_MAX_DEVICES; ++i) {
        fm_pipe_owner *owner = &fm->owners[i];
        if (owner->rid != 0 &&
            local_runtime_fm_lease_state(runtime, owner->lease, &owner->rid) == LOCAL_FM_FREE) {
            *owner = (fm_pipe_owner){0};
        }
    }
}

static gridpico_result reserve_fm(local_runtime *runtime, fm_pipe *fm, uint64_t epoch,
                                  uint64_t rid, const char *hex, reply *response)
{
    uint8_t name[GRIDPICO_PATCH_NAME_BYTES] = {0};
    const size_t length = decode_hex(hex, name, sizeof name - 1);
    if (length == 0 || strspn(hex, "0123456789ABCDEF") != length * 2 ||
        memchr(name, '\0', length) != NULL) return GRIDPICO_INVALID;
    if (epoch != runtime->fm_runtime_epoch || rid <= fm->last_rid) return GRIDPICO_UNAVAILABLE;
    if (runtime->now_ms > UINT64_MAX - FM_RESERVATION_MS) return GRIDPICO_CAPACITY;

    for (size_t i = 0; i < GRIDPICO_PATCH_MAX_DEVICES; ++i) {
        fm_pipe_owner *owner = &fm->owners[i];
        if (owner->rid != 0) continue;
        const gridpico_result result = local_runtime_reserve_fm(
            runtime, (const char *)name, runtime->now_ms + FM_RESERVATION_MS, &owner->rid, &owner->lease);
        if (result != GRIDPICO_INVALID) fm->last_rid = rid;
        if (result == GRIDPICO_OK) {
            owner->rid = rid;
            response->fm_kind = FM_REPLY_RESERVED;
            response->fm_owner = owner;
        }
        return result;
    }
    fm->last_rid = rid;
    return GRIDPICO_CAPACITY;
}

static gridpico_result dispatch_fm(local_runtime *runtime, fm_pipe *fm, char *fields[MAX_FIELDS],
                                   size_t count, reply *response)
{
    const bool probe_open = fm->probe_open;
    fm->probe_open = false;
    if (!fm->prepared) return GRIDPICO_INVALID;
    uint64_t epoch;
    if (strcmp(fields[0], "fm-join-bind") == 0) {
        if (count != 3 || strcmp(fields[1], "fm-v1") != 0 ||
            !canonical_decimal(fields[2], UINT64_MAX, &epoch)) return GRIDPICO_INVALID;
        if (!probe_open || runtime->fm_runtime_epoch != 0) return GRIDPICO_UNAVAILABLE;
        runtime->fm_runtime_epoch = epoch;
        response->fm_kind = FM_REPLY_READY;
        return GRIDPICO_OK;
    }
    if (runtime->fm_runtime_epoch == 0) return GRIDPICO_INVALID;

    uint64_t rid;
    if (count < 4 || !canonical_decimal(fields[1], UINT64_MAX, &epoch) ||
        !canonical_decimal(fields[2], UINT64_MAX, &rid)) return GRIDPICO_INVALID;
    if (strcmp(fields[0], "fm-reserve") == 0) {
        return count == 4 ? reserve_fm(runtime, fm, epoch, rid, fields[3], response) : GRIDPICO_INVALID;
    }

    const bool publish = strcmp(fields[0], "fm-publish") == 0;
    const bool retire = strcmp(fields[0], "fm-retire") == 0;
    uint64_t port, generation, sid = 0;
    if ((!publish && !retire) || count != 6 || !canonical_decimal(fields[3], UINT16_MAX, &port) ||
        !canonical_decimal(fields[4], UINT32_MAX, &generation) || generation <= 1 ||
        (!(retire && strcmp(fields[5], "-") == 0) &&
         !canonical_decimal(fields[5], UINT64_MAX, &sid))) return GRIDPICO_INVALID;
    if (epoch != runtime->fm_runtime_epoch) return GRIDPICO_UNAVAILABLE;

    for (size_t i = 0; i < GRIDPICO_PATCH_MAX_DEVICES; ++i) {
        fm_pipe_owner *owner = &fm->owners[i];
        if (owner->rid != rid || owner->lease.output.endpoint != port ||
            owner->lease.output.generation != generation) continue;
        if (retire) {
            if (owner->sid != sid) return GRIDPICO_UNAVAILABLE;
            return local_runtime_retire_fm(runtime, owner->lease, &owner->rid,
                                            sid == 0 ? NULL : &owner->sid);
        }
        if (owner->sid != 0 || sid <= fm->last_sid) return GRIDPICO_UNAVAILABLE;
        fm->last_sid = sid;
        const gridpico_result result =
            local_runtime_publish_fm(runtime, owner->lease, &owner->rid, &owner->sid);
        if (result == GRIDPICO_OK) {
            owner->sid = sid;
            response->fm_kind = FM_REPLY_PUBLISHED;
            response->fm_owner = owner;
        }
        return result;
    }
    return GRIDPICO_UNAVAILABLE;
}

static gridpico_result dispatch_apc_result(local_runtime *runtime, char *fields[MAX_FIELDS], size_t count)
{
    uint64_t panel, panel_generation, led, led_generation, token;
    if (count != 7 || !canonical_decimal(fields[1], UINT16_MAX, &panel) ||
        !canonical_decimal(fields[2], UINT32_MAX, &panel_generation) ||
        !canonical_decimal(fields[3], UINT16_MAX, &led) ||
        !canonical_decimal(fields[4], UINT32_MAX, &led_generation) ||
        !canonical_decimal(fields[5], UINT64_MAX, &token)) return GRIDPICO_INVALID;
    const bool written = strcmp(fields[6], "written") == 0;
    if (!written && strcmp(fields[6], "dropped") != 0) return GRIDPICO_INVALID;
    return local_runtime_complete_apc_page(
        runtime, (gridpico_connection){(gridpico_port_id)panel, (uint32_t)panel_generation},
        (gridpico_connection){(gridpico_port_id)led, (uint32_t)led_generation}, token, written);
}

static gridpico_result dispatch(local_runtime *runtime, fm_pipe *fm, char *line, reply *response)
{
    char *fields[MAX_FIELDS] = {0};
    const size_t count = split_fields(line, fields);
    if (count < 2) {
        fm->probe_open = false;
        return GRIDPICO_INVALID;
    }
    const char *command = fields[0];
    if (strcmp(command, "forwarding-bind") == 0) {
        if (count != 2 || strcmp(fields[1], "forwarding-v1") != 0) return GRIDPICO_INVALID;
        return local_runtime_bind_forwarding_transport(runtime);
    }
    if (strcmp(command, "apc-page-bind") == 0) {
        if (count != 2 || strcmp(fields[1], "apc-page-v1") != 0) return GRIDPICO_INVALID;
        const gridpico_result result = local_runtime_bind_apc_transport(runtime);
        response->apc_ready = result == GRIDPICO_OK;
        return result;
    }
    if (runtime->profile->editable_surface && !runtime->editable_apc.enabled)
        return GRIDPICO_UNAVAILABLE;
    if (strcmp(command, "apc-page-result") == 0) return dispatch_apc_result(runtime, fields, count);
    if (strncmp(command, "fm-", 3) == 0) return dispatch_fm(runtime, fm, fields, count, response);
    fm->probe_open = false;
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

static bool write_apc_records(local_runtime *runtime)
{
    const local_apc_surface *apc = &runtime->editable_apc;
    if (apc->refusal != GRIDPICO_OK &&
        printf("apc-gesture-refused %u %" PRIu32 " %s\n", (unsigned)apc->refused_owner.endpoint,
               apc->refused_owner.generation, result_line(apc->refusal) + sizeof("err ") - 1) < 0)
        return false;

    const gridpico_apc_surface_message *messages;
    const local_apc_flight *page = local_runtime_take_apc_page(runtime, &messages);
    if (page == NULL) return true;
    if (printf("apc-page %u %" PRIu32 " %u %" PRIu32 " %" PRIu64 " ",
               (unsigned)page->panel.endpoint, page->panel.generation, (unsigned)page->led.endpoint,
               page->led.generation, page->token) < 0) return false;
    for (size_t i = 0; i < GRIDPICO_APC_PHYSICAL_FEEDBACK_FRAMES; ++i) {
        if (!write_hex((const char *)messages[i].bytes, sizeof(messages[i].bytes))) return false;
    }
    return putchar('\n') != EOF;
}

static bool write_records(local_runtime *runtime)
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
            if (row->panic && (!runtime->serial.attached || runtime->serial.fault ||
                runtime->serial.generation != runtime->panic_owner)) continue;
            if (printf("midi %u %" PRIu32 " ", port, generation) < 0) return false;
            if (runtime->forwarding_enabled) {
                const int prefix = row->forwarding_revision != 0
                    ? printf("ordinary %" PRIu64 " ", row->forwarding_revision)
                    : fputs(row->panic ? "panic - " : "control - ", stdout);
                if (prefix < 0) return false;
            }
            if (!write_hex((const char *)row->bytes, sizeof(row->bytes)) || putchar('\n') == EOF) return false;
        }
    }
    const bool serial_written = runtime->serial.fault
        ? printf("fault serial %" PRIu32 "\n", runtime->serial.generation) >= 0
        : write_serial_range(runtime, serial_offset, runtime->serial_count);
    return serial_written && write_apc_records(runtime);
}

static bool write_event(local_runtime *runtime)
{
    return puts("event") != EOF && write_records(runtime) && puts("end") != EOF &&
           fflush(stdout) == 0;
}

static uint64_t next_deadline(const local_runtime *runtime)
{
    const uint64_t raw = runtime->raw_deadline_ms;
    const uint64_t fm = local_runtime_fm_deadline(runtime);
    const uint64_t deadline = fm != 0 && (raw == 0 || fm < raw) ? fm : raw;
    const uint64_t apc = runtime->editable_apc.refresh_deadline_ms;
    return apc != 0 && (deadline == 0 || apc < deadline) ? apc : deadline;
}

static bool expire(local_runtime *runtime, fm_pipe *fm, uint64_t now)
{
    const uint64_t deadline = next_deadline(runtime);
    if (deadline == 0 || now < deadline) return true;
    const bool raw_due = runtime->raw_deadline_ms != 0 && now >= runtime->raw_deadline_ms;
    const uint64_t apc_deadline = runtime->editable_apc.refresh_deadline_ms;
    const bool apc_due = apc_deadline != 0 && now >= apc_deadline;
    local_runtime_begin(runtime, now);
    const gridpico_result expired = local_runtime_expire(runtime);
    if ((raw_due || apc_due) && local_runtime_finish(runtime, expired) != GRIDPICO_OK && raw_due) {
        fputs("Local router could not restore feedback after Raw expiry\n", stderr);
    }
    release_unused_fm_owners(runtime, fm);
    const local_apc_surface *apc = &runtime->editable_apc;
    return !(raw_due || (apc_due && apc->latest_valid && apc->flight.token == 0)) || write_event(runtime);
}

static bool settle_fm_reply(local_runtime *runtime, const reply *response, gridpico_result *result)
{
    const fm_pipe_owner *owner = response->fm_owner;
    if (owner == NULL) return response->fm_kind != FM_REPLY_READY || *result == GRIDPICO_OK;
    const local_fm_state state = local_runtime_fm_lease_state(runtime, owner->lease, &owner->rid);
    const bool published = response->fm_kind == FM_REPLY_PUBLISHED;
    if (*result == GRIDPICO_OK && state == (published ? LOCAL_FM_ATTACHED : LOCAL_FM_PENDING)) return true;

    /* An error must never leave this command's allocation in service. */
    if (*result == GRIDPICO_OK) *result = GRIDPICO_UNAVAILABLE;
    return state == LOCAL_FM_FREE ||
        local_runtime_retire_fm(runtime, owner->lease, &owner->rid, published ? &owner->sid : NULL) == GRIDPICO_OK;
}

static bool write_fm_reply(const local_runtime *runtime, const reply *response)
{
    const fm_pipe_owner *owner = response->fm_owner;
    switch (response->fm_kind) {
    case FM_REPLY_READY:
        return printf("fm-join-ready fm-v1 %" PRIu64 "\n", runtime->fm_runtime_epoch) >= 0;
    case FM_REPLY_RESERVED:
        return printf("fm-reserved %" PRIu64 " %" PRIu64 " %u %u %" PRIu32 " %d\n",
                      owner->lease.runtime_epoch, owner->rid, (unsigned)owner->lease.device,
                      (unsigned)owner->lease.output.endpoint, owner->lease.output.generation,
                      FM_RESERVATION_MS) >= 0;
    case FM_REPLY_PUBLISHED:
        return printf("fm-published %" PRIu64 " %" PRIu64 " %u %" PRIu32 " %" PRIu64 " %d\n",
                      owner->lease.runtime_epoch, owner->rid, (unsigned)owner->lease.output.endpoint,
                      owner->lease.output.generation, owner->sid, LOCAL_MIDI_QUEUE) >= 0;
    case FM_REPLY_NONE:
        return true;
    }
    return false;
}

static bool handle_line(local_runtime *runtime, fm_pipe *fm, char *line, bool malformed)
{
    uint64_t now;
    if (!monotonic_ms(&now) || !expire(runtime, fm, now)) {
        return false;
    }
    local_runtime_begin(runtime, now);
    reply response = {0};
    if (malformed) fm->probe_open = false;
    const gridpico_result dispatched = malformed ? GRIDPICO_INVALID
                                                  : dispatch(runtime, fm, line, &response);
    gridpico_result result = local_runtime_finish(runtime, dispatched);
    if (!settle_fm_reply(runtime, &response, &result)) return false;
    if (result == GRIDPICO_OK && !write_fm_reply(runtime, &response)) return false;
    if (response.apc_ready && puts("apc-page-ready apc-page-v1") == EOF) return false;
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
    if (runtime->fm_runtime_epoch != 0) release_unused_fm_owners(runtime, fm);
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

static bool service_panic(local_runtime *runtime, uint64_t now)
{
    if (runtime->panic_report.state != GRIDPICO_PANIC_STAGE_RUNNING) return true;
    local_runtime_begin(runtime, now);
    local_runtime_service_panic(runtime);
    return runtime->output_count == 0 || write_event(runtime);
}

static bool serve(local_runtime *runtime, const local_router_service *service, fm_pipe *fm)
{
    char line[MAX_LINE_BYTES + 1];
    size_t position = 0;
    bool malformed = false;
    for (;;) {
        uint64_t now;
        if (!monotonic_ms(&now) || !expire(runtime, fm, now) ||
            !service_adapter(runtime, service, now)) {
            return false;
        }
        const uint64_t deadline = next_deadline(runtime);
        const uint64_t remaining = deadline > now ? deadline - now : 0;
        int wait = deadline == 0 ? -1 : remaining > INT_MAX ? INT_MAX : (int)remaining;
        if (service != NULL && (wait < 0 || wait > service->interval_ms)) {
            wait = service->interval_ms;
        }
        if (runtime->panic_report.state == GRIDPICO_PANIC_STAGE_RUNNING) wait = 0;
        struct pollfd input = {.fd = STDIN_FILENO, .events = POLLIN};
        const int ready = poll(&input, 1, wait);
        if (ready == 0 || (ready < 0 && errno == EINTR)) {
            if (!service_panic(runtime, now)) return false;
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
                if (!handle_line(runtime, fm, line, malformed)) {
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
        if (!service_panic(runtime, now)) return false;
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

static int run(local_runtime *runtime, const local_router_service *service, bool ui_descriptor,
               bool fm_prepared)
{
    fm_pipe fm = {.prepared = fm_prepared, .probe_open = fm_prepared};
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR || (ui_descriptor && !write_ui_roles(runtime))) {
        return EXIT_FAILURE;
    }
    if (runtime->profile->editable_surface &&
        (!ui_descriptor || !runtime->forwarding_transport ||
         puts("apc-page-required apc-page-v1") == EOF)) return EXIT_FAILURE;
    const int ready = runtime->configured
        ? printf("ready %zu\n", runtime->profile->limits.midi_peer_limit) : puts("ready");
    const bool complete = ready >= 0 && fflush(stdout) == 0 &&
        (runtime->output_count == 0 || write_event(runtime)) && serve(runtime, service, &fm);
    local_runtime_begin(runtime, 0);
    const gridpico_result retired = local_runtime_retire(runtime);
    const bool flushed = complete && (runtime->output_count == 0 || write_event(runtime));
    if (!flushed || retired != GRIDPICO_OK) {
        fputs("Local router stopped with a pipe or detach error\n", stderr);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int local_router_run(local_runtime *runtime, const local_router_service *service, bool ui_descriptor)
{
    return run(runtime, service, ui_descriptor, false);
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
        if (profile.editable_surface && (!web_ui || !ui_descriptor || recorder)) {
            fputs("Editable APC requires an explicitly prepared Web UI profile\n", stderr);
            return EXIT_FAILURE;
        }
        if (web_ui && !local_profile_supports_web_ui(&profile)) {
            fputs("Router profile must offer the six starter socket roles at ports 1–6 for --web-ui\n",
                  stderr);
            return EXIT_FAILURE;
        }
    }
    local_runtime runtime;
    const bool fm_prepared = path != NULL && web_ui && ui_descriptor && !recorder && profile.passive_surface;
    const gridpico_result initialized = fm_prepared
        ? local_runtime_prepare_fm_profile(&runtime, &profile)
        : local_runtime_init_profile(&runtime, path != NULL ? &profile : NULL);
    if (initialized != GRIDPICO_OK) {
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
    runtime.forwarding_transport = ui_descriptor && !recorder;
    return run(&runtime, NULL, ui_descriptor, fm_prepared);
}
#endif
