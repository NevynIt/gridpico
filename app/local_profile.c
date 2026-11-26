#define _POSIX_C_SOURCE 200809L
#include "app/local_profile.h"

#include "api/patchbay_controls.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { PROFILE_BYTES = 8192, PROFILE_LINE_BYTES = 160 };

typedef struct {
    const char *token;
    gridpico_local_port_capability capability;
    gridpico_patch_direction direction;
} role_kind;

static const role_kind roles[LOCAL_ROLE_COUNT] = {
    [LOCAL_ROLE_AUX] = {"aux", GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_PATCH_INPUT},
    [LOCAL_ROLE_KEYBED] = {"keybed", GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_PATCH_INPUT},
    [LOCAL_ROLE_PANEL] = {"panel", GRIDPICO_LOCAL_PORT_PANEL, GRIDPICO_PATCH_INPUT},
    [LOCAL_ROLE_LED] = {"led", GRIDPICO_LOCAL_PORT_RAW, GRIDPICO_PATCH_OUTPUT},
    [LOCAL_ROLE_SEQUENCE] = {"sequence", GRIDPICO_LOCAL_PORT_RAW, GRIDPICO_PATCH_INPUT},
    [LOCAL_ROLE_MONITOR] = {"monitor", GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_PATCH_OUTPUT},
    [LOCAL_ROLE_LOG] = {"log", GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_PATCH_OUTPUT},
    [LOCAL_ROLE_FM] = {"fm", GRIDPICO_LOCAL_PORT_MANAGED, GRIDPICO_PATCH_OUTPUT},
};

static bool space(char byte)
{
    return byte == ' ' || byte == '\t' || byte == '\r';
}

static char *word(char **rest)
{
    char *next = *rest;
    while (space(*next)) ++next;
    char *start = next;
    while (*next != '\0' && !space(*next)) ++next;
    if (*next != '\0') *next++ = '\0';
    *rest = next;
    return start;
}

static bool integer(char **rest, unsigned maximum, unsigned *value)
{
    const char *text = word(rest);
    if (*text == '\0') return false;
    unsigned parsed = 0;
    for (; *text >= '0' && *text <= '9'; ++text) {
        const unsigned digit = (unsigned)(*text - '0');
        if (parsed > maximum / 10u || (parsed == maximum / 10u && digit > maximum % 10u)) {
            return false;
        }
        parsed = 10u * parsed + digit;
    }
    if (*text != '\0') return false;
    *value = parsed;
    return true;
}

static bool copy_name(char name[GRIDPICO_PATCH_NAME_BYTES], char *value)
{
    while (space(*value)) ++value;
    size_t bytes = strlen(value);
    while (bytes != 0 && space(value[bytes - 1])) --bytes;
    if (bytes == 0 || bytes >= GRIDPICO_PATCH_NAME_BYTES) return false;
    for (size_t i = 0; i < bytes; ++i) {
        if (value[i] == '\t' || value[i] == '\r') return false;
    }
    memcpy(name, value, bytes);
    name[bytes] = '\0';
    return true;
}

gridpico_port_id local_profile_role(const local_router_profile *profile, local_port_role role)
{
    for (size_t i = 0; i < profile->registry.inventory.port_count; ++i) {
        if (profile->ports[i].role == role) return profile->registry.inventory.ports[i].id;
    }
    return 0;
}

const char *local_profile_role_token(local_port_role role)
{
    return roles[role].token;
}

static bool read_device(local_router_profile *profile, char *rest)
{
    gridpico_patch_inventory *inventory = &profile->registry.inventory;
    unsigned id;
    if (inventory->device_count >= GRIDPICO_PATCH_MAX_DEVICES ||
        !integer(&rest, UINT16_MAX, &id) || id == 0) return false;
    for (size_t i = 0; i < inventory->device_count; ++i) {
        if (inventory->devices[i].id == id) return false;
    }
    gridpico_patch_device *row = &inventory->devices[inventory->device_count];
    if (!copy_name(row->name, rest)) return false;
    row->id = (gridpico_device_id)id;
    row->state = GRIDPICO_PATCH_DISCONNECTED;
    ++inventory->device_count;
    return true;
}

static bool read_port(local_router_profile *profile, char *rest)
{
    gridpico_patch_inventory *inventory = &profile->registry.inventory;
    unsigned id, device, limit;
    if (inventory->port_count >= GRIDPICO_PATCH_MAX_PORTS ||
        !integer(&rest, UINT16_MAX, &id) || id == 0 ||
        !integer(&rest, UINT16_MAX, &device) || device == 0) return false;
    const char *direction_word = word(&rest);
    const char *capability_word = word(&rest);
    if (!integer(&rest, LOCAL_PANEL_QUEUE, &limit) || limit == 0) return false;
    const char *role_word = word(&rest);
    const char *access = word(&rest);
    const bool socket = strcmp(access, "socket") == 0;
    if (!socket && strcmp(access, "unavailable") != 0) return false;

    const bool input = strcmp(direction_word, "in") == 0;
    if (!input && strcmp(direction_word, "out") != 0) return false;
    const gridpico_patch_direction direction = input ? GRIDPICO_PATCH_INPUT : GRIDPICO_PATCH_OUTPUT;
    size_t role = 0;
    while (role < LOCAL_ROLE_COUNT && strcmp(roles[role].token, role_word) != 0) ++role;
    if (role == LOCAL_ROLE_COUNT || (role == LOCAL_ROLE_FM && !socket) || (role != LOCAL_ROLE_AUX &&
        (roles[role].direction != direction || local_profile_role(profile, (local_port_role)role) != 0))) {
        return false;
    }
    const gridpico_local_port_capability capability = roles[role].capability;
    static const char *const capability_tokens[] = {
        [GRIDPICO_LOCAL_PORT_MANAGED] = "managed", [GRIDPICO_LOCAL_PORT_RAW] = "raw",
        [GRIDPICO_LOCAL_PORT_PANEL] = "panel",
    };
    if (strcmp(capability_tokens[capability], capability_word) != 0) return false;
    for (size_t i = 0; i < inventory->port_count; ++i) {
        if (inventory->ports[i].id == id) return false;
    }
    const size_t slot = inventory->port_count;
    gridpico_patch_port *row = &inventory->ports[slot];
    if (!copy_name(row->name, rest)) return false;
    row->id = (gridpico_port_id)id;
    row->device = (gridpico_device_id)device;
    row->direction = direction;
    row->state = GRIDPICO_PATCH_DISCONNECTED;
    row->patch_eligible = capability == GRIDPICO_LOCAL_PORT_MANAGED;
    profile->registry.port_capabilities[slot] = capability;
    profile->ports[slot] = (local_profile_port){(local_port_role)role, limit, socket};
    ++inventory->port_count;
    return true;
}

static bool read_limits(gridpico_local_profile *limits, char *rest)
{
    const unsigned ceilings[] = {
        GRIDPICO_PATCH_MAX_PORTS, GRIDPICO_LOCAL_MAX_MANAGED_BRANCHES, GRIDPICO_LOCAL_MAX_RAW_BRANCHES,
        GRIDPICO_LOCAL_MAX_FANOUT, GRIDPICO_LOCAL_MAX_NOTE_OCCURRENCES, GRIDPICO_LOCAL_MAX_RELEASE_CREDITS,
    };
    unsigned values[sizeof(ceilings) / sizeof(*ceilings)];
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); ++i) {
        if (!integer(&rest, ceilings[i], &values[i])) return false;
    }
    if (values[0] == 0 || *word(&rest) != '\0') return false;
    *limits = (gridpico_local_profile){
        .midi_peer_limit = values[0],
        .managed_branch_limit = values[1],
        .raw_branch_limit = values[2],
        .managed_fanout_limit = values[3],
        .occurrence_limit = values[4],
        .release_credit_limit = values[5],
        .dynamic_device_limit = 0,
        .dynamic_port_limit = 0,
    };
    return true;
}

static bool read_row(local_router_profile *profile, char *rest, bool *surface_seen)
{
    const char *kind = word(&rest);
    if (*kind == '\0' || *kind == '#') return true;
    if (strcmp(kind, "device") == 0) return read_device(profile, rest);
    if (strcmp(kind, "port") == 0) return read_port(profile, rest);
    if (strcmp(kind, "limits") == 0) {
        return profile->limits.midi_peer_limit == 0 && read_limits(&profile->limits, rest);
    }
    if (strcmp(kind, "note-transform") == 0) {
        if (profile->note_transform || strcmp(word(&rest), "on") != 0 ||
            *word(&rest) != '\0') return false;
        profile->note_transform = true;
        return true;
    }
    if (strcmp(kind, "surface") != 0 || *surface_seen) return false;
    const char *mode = word(&rest);
    if ((strcmp(mode, "passive") != 0 && strcmp(mode, "legacy") != 0) ||
        *word(&rest) != '\0') return false;
    profile->passive_surface = strcmp(mode, "passive") == 0;
    *surface_seen = true;
    return true;
}

static bool valid_resources(const local_router_profile *profile)
{
    size_t claimable = 0;
    size_t cleanup = profile->limits.occurrence_limit;
    if (cleanup > profile->limits.release_credit_limit) cleanup = profile->limits.release_credit_limit;
    const gridpico_patch_inventory *inventory = &profile->registry.inventory;
    for (size_t i = 0; i < inventory->port_count; ++i) {
        const local_profile_port port = profile->ports[i];
        if (!port.socket) continue;
        ++claimable;
        if ((port.role == LOCAL_ROLE_LED && port.queue_limit < GRIDPICO_PATCH_LED_FRAMES) ||
            (inventory->ports[i].direction == GRIDPICO_PATCH_OUTPUT &&
             profile->registry.port_capabilities[i] == GRIDPICO_LOCAL_PORT_MANAGED &&
             profile->limits.managed_branch_limit != 0 && port.queue_limit < cleanup)) return false;
    }
    return profile->limits.midi_peer_limit != 0 && profile->limits.midi_peer_limit <= claimable;
}

bool local_profile_load(const char *path, local_router_profile *profile)
{
    const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    struct stat status;
    if (fd < 0 || fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_size > PROFILE_BYTES) {
        if (fd >= 0) close(fd);
        fprintf(stderr, "Router profile needs a readable regular file up to %d bytes: %s\n", PROFILE_BYTES, path);
        return false;
    }
    FILE *file = fdopen(fd, "r");
    if (file == NULL) {
        close(fd);
        perror("Cannot read router profile");
        return false;
    }
    *profile = (local_router_profile){0};
    bool surface_seen = false;
    bool parsed = true;
    char row[PROFILE_LINE_BYTES + 1];
    size_t size = 0, length = 0, number = 1;
    for (int byte = fgetc(file); byte != EOF; byte = fgetc(file)) {
        if (++size > PROFILE_BYTES) {
            parsed = false;
            break;
        }
        if (byte == '\n') {
            row[length] = '\0';
            if (!read_row(profile, row, &surface_seen)) {
                parsed = false;
                break;
            }
            length = 0;
            ++number;
        } else if (length == PROFILE_LINE_BYTES ||
                   ((byte < 0x20 || byte == 0x7f) && byte != '\t' && byte != '\r')) {
            parsed = false;
            break;
        } else {
            row[length++] = (char)byte;
        }
    }
    row[length] = '\0';
    parsed = parsed && !ferror(file) && read_row(profile, row, &surface_seen);
    fclose(file);
    if (!parsed) {
        fprintf(stderr, "%s:%zu: invalid or duplicate router profile row\n", path, number);
        return false;
    }
    if (!surface_seen || !valid_resources(profile)) {
        fprintf(stderr, "%s: missing surface or invalid peer/queue budget\n", path);
        return false;
    }
    return true;
}

bool local_profile_supports_web_ui(const local_router_profile *profile)
{
    static const gridpico_port_id starter_ids[] = {
        [LOCAL_ROLE_KEYBED] = 1, [LOCAL_ROLE_PANEL] = 2, [LOCAL_ROLE_LED] = 3,
        [LOCAL_ROLE_SEQUENCE] = 4, [LOCAL_ROLE_MONITOR] = 5, [LOCAL_ROLE_LOG] = 6,
    };
    if (profile->limits.midi_peer_limit < GRIDPICO_LOCAL_PORTS) return false;
    for (size_t role = LOCAL_ROLE_KEYBED; role < sizeof starter_ids / sizeof starter_ids[0]; ++role) {
        bool matched = false;
        for (size_t i = 0; i < profile->registry.inventory.port_count; ++i) {
            if (profile->ports[i].role == role && profile->ports[i].socket &&
                profile->registry.inventory.ports[i].id == starter_ids[role]) matched = true;
        }
        if (!matched) return false;
    }
    return true;
}
