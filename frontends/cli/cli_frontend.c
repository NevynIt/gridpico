#define EMBEDDED_CLI_IMPL
#include "frontends/cli/cli_frontend.h"

#include <stdio.h>
#include <string.h>

_Static_assert(GRIDPICO_CLI_LINE_BYTES <= UINT16_MAX,
               "embedded-cli uses 16-bit configured capacities");

static const char *connection_name(gridpico_patch_connection_state state)
{
    switch (state) {
    case GRIDPICO_PATCH_CONNECTED:
        return "connected";
    case GRIDPICO_PATCH_DISCONNECTED:
        return "disconnected";
    case GRIDPICO_PATCH_AMBIGUOUS:
        return "ambiguous";
    }
    return NULL;
}

static bool bounded_name_length(const char name[GRIDPICO_PATCH_NAME_BYTES], size_t *length)
{
    for (size_t i = 0; i < GRIDPICO_PATCH_NAME_BYTES; ++i) {
        if (name[i] == '\0') {
            *length = i;
            return true;
        }
    }
    return false;
}

static bool write_bytes(gridpico_cli_write_fn write, void *context,
                        const char *bytes, size_t size)
{
    if (write == NULL || bytes == NULL) {
        return false;
    }
    write(context, bytes, size);
    return true;
}

static bool write_text(gridpico_cli_write_fn write, void *context, const char *text)
{
    return write_bytes(write, context, text, strlen(text));
}

static bool write_record(gridpico_cli_write_fn write, void *context,
                         const char *record, size_t size)
{
    return write_bytes(write, context, record, size) &&
           write_bytes(write, context, "\r\n", 2);
}

typedef struct {
    enum {
        CLI_COMMAND_HELP,
        CLI_COMMAND_INVENTORY,
        CLI_COMMAND_STATUS,
        CLI_COMMAND_LOGS,
        CLI_COMMAND_COLOR,
        CLI_COMMAND_ROUTE,
        CLI_COMMAND_ROUTING,
        CLI_COMMAND_TRACE,
        CLI_COMMAND_HUBTRACE,
        CLI_COMMAND_BOOTSEL
    } command;
    const char *name;
    const char *detail;
} cli_help_entry;

static const cli_help_entry help_inventory = {
    CLI_COMMAND_INVENTORY, "inventory", "inventory -- list devices and ports"};
static const cli_help_entry help_status = {
    CLI_COMMAND_STATUS, "status", "status -- show the current Patch status"};
static const cli_help_entry help_logs = {
    CLI_COMMAND_LOGS, "logs", "logs -- read retained diagnostic records"};
static const cli_help_entry help_color = {
    CLI_COMMAND_COLOR, "color", "color get <device> | color set <device> #RRGGBB -- read or set a device color"};
static const cli_help_entry help_color_readonly = {
    CLI_COMMAND_COLOR, "color", "color get <device> -- read a device color"};
static const cli_help_entry help_route = {
    CLI_COMMAND_ROUTE, "route", "route <src> <dst> managed|raw [on|off] -- read or set a route, then read back its state"};
static const cli_help_entry help_route_guarded = {
    CLI_COMMAND_ROUTE, "route", "route <src> <dst> managed|raw [on|off]; route <src> <dst> managed on|off if <revision> -- read or set a route with revision guard, then read back committed state"};
static const cli_help_entry help_routing = {
    CLI_COMMAND_ROUTING, "routing", "routing | routing <revision> <offset> -- read a bounded Routing snapshot page"};
static const cli_help_entry help_trace = {
    CLI_COMMAND_TRACE, "trace", "trace -- request platform TRACE diagnostics"};
static const cli_help_entry help_hubtrace = {
    CLI_COMMAND_HUBTRACE, "hubtrace", "hubtrace -- request platform HUBTRACE diagnostics"};
static const cli_help_entry help_bootsel = {
    CLI_COMMAND_BOOTSEL, "BOOTSEL", "BOOTSEL -- exact uppercase bytes plus LF after DTR; handled by the platform byte parser"};
static const cli_help_entry help_self = {
    CLI_COMMAND_HELP, "help", "help [command] -- list available commands or show command forms"};
static const cli_help_entry help_color_get = {
    CLI_COMMAND_COLOR, "color get", "color get <device> -- read a device color"};
static const cli_help_entry help_color_set = {
    CLI_COMMAND_COLOR, "color set", "color set <device> #RRGGBB -- set and read back a device color"};

static const cli_help_entry *const command_table[] = {
    &help_self, &help_inventory, &help_status, &help_logs, &help_color,
    &help_route, &help_routing, &help_trace, &help_hubtrace, &help_bootsel
};

static const cli_help_entry *command_for_line(const char *line)
{
    const char *end = strchr(line, ' ');
    const size_t name_size = end == NULL ? strlen(line) : (size_t)(end - line);
    for (size_t i = 0; i < sizeof command_table / sizeof command_table[0]; ++i) {
        const cli_help_entry *entry = command_table[i];
        if (strlen(entry->name) == name_size &&
            memcmp(entry->name, line, name_size) == 0) return entry;
    }
    if ((name_size == 5 && memcmp(line, "TRACE", 5) == 0)) return &help_trace;
    if (name_size == 8 && memcmp(line, "HUBTRACE", 8) == 0) return &help_hubtrace;
    return NULL;
}

static size_t help_entries(const gridpico_cli_frontend *frontend,
                           const cli_help_entry **entries, size_t capacity)
{
    size_t count = 0;
    if (capacity < 3) return 0;
    entries[count++] = &help_self;
    entries[count++] = &help_inventory;
    entries[count++] = &help_status;
    if (frontend->read_logs != NULL && count < capacity) entries[count++] = &help_logs;
    if (frontend->read_device_color != NULL && count < capacity) {
        entries[count++] = frontend->set_device_color != NULL
                               ? &help_color : &help_color_readonly;
    }
    const bool target_callbacks = frontend->routing_read_route != NULL ||
                                  frontend->routing_set_route != NULL ||
                                  frontend->routing_read != NULL;
    const bool route_pair = target_callbacks
        ? frontend->routing_read_route != NULL && frontend->routing_set_route != NULL
        : frontend->read_local_route != NULL && frontend->set_local_route != NULL;
    if (route_pair && count < capacity) {
        const bool guarded = frontend->routing_read != NULL &&
                             frontend->routing_read_route != NULL &&
                             frontend->routing_set_route != NULL &&
                             frontend->routing_scratch != NULL;
        entries[count++] = guarded ? &help_route_guarded : &help_route;
    }
    const bool routing_available = frontend->routing_read != NULL
        ? frontend->routing_scratch != NULL
        : frontend->read_local_routing != NULL;
    if (routing_available && count < capacity)
        entries[count++] = &help_routing;
    if (frontend->start_platform != NULL &&
        (frontend->platform_capabilities & GRIDPICO_CLI_PLATFORM_TRACE) != 0 &&
        count < capacity) entries[count++] = &help_trace;
    if (frontend->start_platform != NULL &&
        (frontend->platform_capabilities & GRIDPICO_CLI_PLATFORM_HUBTRACE) != 0 &&
        count < capacity) entries[count++] = &help_hubtrace;
    if ((frontend->platform_capabilities & GRIDPICO_CLI_PLATFORM_BOOTSEL) != 0 &&
        count < capacity) entries[count++] = &help_bootsel;
    return count;
}

static gridpico_result execute_help(gridpico_cli_frontend *frontend, const char *topic,
                                    gridpico_cli_write_fn write, void *context)
{
    const cli_help_entry *entries[10];
    const size_t count = help_entries(frontend, entries, sizeof entries / sizeof entries[0]);
    if (topic != NULL) {
        if (strcmp(topic, "TRACE") == 0) topic = "trace";
        else if (strcmp(topic, "HUBTRACE") == 0) topic = "hubtrace";
        const cli_help_entry *selected = NULL;
        if (strcmp(topic, "color get") == 0) {
            if (frontend->read_device_color == NULL) return GRIDPICO_UNAVAILABLE;
            selected = &help_color_get;
        } else if (strcmp(topic, "color set") == 0) {
            if (frontend->read_device_color == NULL || frontend->set_device_color == NULL)
                return GRIDPICO_UNAVAILABLE;
            selected = &help_color_set;
        } else {
            for (size_t i = 0; i < count; ++i) {
                if (strcmp(entries[i]->name, topic) == 0) {
                    selected = entries[i];
                    break;
                }
            }
            if (selected == NULL) {
                static const char *const known[] = {
                    "help", "inventory", "status", "logs", "color", "route",
                    "routing", "trace", "hubtrace", "BOOTSEL"
                };
                for (size_t i = 0; i < sizeof known / sizeof known[0]; ++i) {
                    if (strcmp(known[i], topic) == 0) return GRIDPICO_UNAVAILABLE;
                }
                return GRIDPICO_INVALID;
            }
        }
        char record[GRIDPICO_CLI_RECORD_BYTES];
        const int size = snprintf(record, sizeof record, "COMMAND %s", selected->detail);
        if (size < 0 || (size_t)size >= sizeof record) return GRIDPICO_CAPACITY;
        return write_record(write, context, record, (size_t)size)
                   ? GRIDPICO_OK : GRIDPICO_UNAVAILABLE;
    }
    for (size_t i = 0; i < count; ++i) {
        char record[GRIDPICO_CLI_RECORD_BYTES];
        const int size = snprintf(record, sizeof record, "COMMAND %s", entries[i]->detail);
        if (size < 0 || (size_t)size >= sizeof record) return GRIDPICO_CAPACITY;
        if (!write_record(write, context, record, (size_t)size)) return GRIDPICO_UNAVAILABLE;
    }
    return GRIDPICO_OK;
}

static bool write_quoted_name(gridpico_cli_write_fn write, void *context,
                              const char name[GRIDPICO_PATCH_NAME_BYTES])
{
    size_t length = 0;
    if (!bounded_name_length(name, &length) || !write_text(write, context, "\"")) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = (unsigned char)name[i];
        if (c == '"' || c == '\\') {
            const char escaped[2] = {'\\', (char)c};
            if (!write_bytes(write, context, escaped, sizeof escaped)) {
                return false;
            }
        } else if (c < 0x20 || c == 0x7f) {
            char escaped[7];
            const int count = snprintf(escaped, sizeof escaped, "\\u%04X", c);
            if (count != 6 || !write_bytes(write, context, escaped, 6)) {
                return false;
            }
        } else if (!write_bytes(write, context, (const char *)&name[i], 1)) {
            return false;
        }
    }
    return write_text(write, context, "\"");
}

static int find_device_index(const gridpico_patch_inventory *inventory,
                             gridpico_device_id id)
{
    for (size_t i = 0; i < inventory->device_count; ++i) {
        if (inventory->devices[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

static bool format_device_record(gridpico_cli_write_fn write, void *context,
                                 const gridpico_patch_device *device,
                                 const gridpico_device_color *color)
{
    char record[GRIDPICO_CLI_RECORD_BYTES];
    const char *state = connection_name(device->state);
    char color_text[8];
    size_t name_length = 0;
    if (state == NULL || !bounded_name_length(device->name, &name_length)) {
        return false;
    }
    if (color->is_set) {
        const int color_size = snprintf(color_text, sizeof color_text, "#%02X%02X%02X",
                                        color->rgb.red, color->rgb.green, color->rgb.blue);
        if (color_size != 7) {
            return false;
        }
    } else {
        memcpy(color_text, "unset", sizeof "unset");
    }
    const int size = snprintf(record, sizeof record, "DEVICE %u %s %s ",
                              (unsigned)device->id, state, color_text);
    if (size < 0 || (size_t)size >= sizeof record ||
        !write_bytes(write, context, record, (size_t)size) ||
        !write_quoted_name(write, context, device->name)) {
        return false;
    }
    return write_record(write, context, "", 0);
}

static bool port_assignment(const gridpico_patch_snapshot *snapshot,
                            const gridpico_patch_port *port, char *output, size_t capacity)
{
    if (!port->patch_eligible) {
        return snprintf(output, capacity, "-") >= 0;
    }
    size_t position = 0;
    for (size_t i = 0; i < snapshot->inventory.port_count; ++i) {
        const gridpico_patch_port *candidate = &snapshot->inventory.ports[i];
        if (!candidate->patch_eligible || candidate->direction != port->direction) {
            continue;
        }
        if (candidate->id == port->id) {
            const char *kind = port->direction == GRIDPICO_PATCH_INPUT ? "row" : "col";
            const int size = snprintf(output, capacity, "%s:1:%zu", kind, position + 1);
            return size >= 0 && (size_t)size < capacity;
        }
        ++position;
    }
    return false;
}

static bool format_port_record(gridpico_cli_write_fn write, void *context,
                               const gridpico_patch_snapshot *snapshot,
                               const gridpico_patch_port *port,
                               const gridpico_apc_patch_snapshot *apc)
{
    char record[GRIDPICO_CLI_RECORD_BYTES];
    char assignment[24];
    const char *state = connection_name(port->state);
    const char *direction = port->direction == GRIDPICO_PATCH_INPUT ? "in" : "out";
    if (state == NULL || port->direction > GRIDPICO_PATCH_OUTPUT) {
        return false;
    }
    if (apc == NULL) {
        if (!port_assignment(snapshot, port, assignment, sizeof assignment)) return false;
    } else {
        const gridpico_port_id *slots = port->direction == GRIDPICO_PATCH_INPUT
                                            ? apc->layout.rows : apc->layout.columns;
        const size_t slot_count = port->direction == GRIDPICO_PATCH_INPUT
                                      ? GRIDPICO_PATCH_SOURCE_ROWS
                                      : GRIDPICO_PATCH_DESTINATION_COLUMNS;
        size_t slot = 0;
        while (slot < slot_count && slots[slot] != port->id) ++slot;
        if (slot == slot_count) {
            memcpy(assignment, "-", 2);
        } else {
            const char *kind = port->direction == GRIDPICO_PATCH_INPUT ? "row" : "col";
            const int count = snprintf(assignment, sizeof assignment, "%s:1:%zu", kind, slot + 1);
            if (count < 0 || (size_t)count >= sizeof assignment) return false;
        }
    }
    const int size = snprintf(record, sizeof record, "PORT %u %u %s %s %s ",
                              (unsigned)port->id, (unsigned)port->device,
                              direction, state, assignment);
    if (size < 0 || (size_t)size >= sizeof record ||
        !write_bytes(write, context, record, (size_t)size) ||
        !write_quoted_name(write, context, port->name)) {
        return false;
    }
    return write_record(write, context, "", 0);
}

static gridpico_result write_inventory(gridpico_cli_frontend *frontend,
                                       gridpico_cli_write_fn write, void *context)
{
    gridpico_patch_snapshot *snapshot = &frontend->snapshot_scratch;
    const gridpico_result result = frontend->read_snapshot(frontend->session_context, snapshot);
    if (result != GRIDPICO_OK) {
        return result;
    }
    if (snapshot->inventory.device_count > GRIDPICO_PATCH_MAX_DEVICES ||
        snapshot->inventory.port_count > GRIDPICO_PATCH_MAX_PORTS) {
        return GRIDPICO_CAPACITY;
    }
    for (size_t p = 0; p < snapshot->inventory.port_count; ++p) {
        if (find_device_index(&snapshot->inventory, snapshot->inventory.ports[p].device) < 0) {
            return GRIDPICO_INVALID;
        }
    }
    gridpico_device_color colors[GRIDPICO_PATCH_MAX_DEVICES] = {{0}};
    for (size_t i = 0; i < snapshot->inventory.device_count; ++i) {
        if (frontend->read_device_color != NULL) {
            const gridpico_result color_result = frontend->read_device_color(
                frontend->session_context, snapshot->inventory.devices[i].id, &colors[i]);
            if (color_result != GRIDPICO_OK) return color_result;
        }
    }
    gridpico_apc_patch_snapshot *apc = frontend->read_apc_patch == NULL
                                          ? NULL : frontend->apc_patch_scratch;
    if (frontend->read_apc_patch != NULL) {
        if (apc == NULL) return GRIDPICO_UNAVAILABLE;
        const gridpico_result apc_result = frontend->read_apc_patch(frontend->session_context, apc);
        if (apc_result != GRIDPICO_OK) return apc_result;
        if (apc->port_count > GRIDPICO_PATCH_MAX_PORTS) return GRIDPICO_CAPACITY;
    }
    if (frontend->read_device_color == NULL && apc == NULL) return GRIDPICO_UNAVAILABLE;
    for (size_t i = 0; i < snapshot->inventory.device_count; ++i) {
        const gridpico_patch_device *device = &snapshot->inventory.devices[i];
        gridpico_device_color unsupported_color = {0};
        const gridpico_device_color *color = frontend->read_device_color == NULL
                                                 ? &unsupported_color : &colors[i];
        if (frontend->read_device_color == NULL) {
            /* Pico has no color preference provider; expose that distinction. */
            char record[GRIDPICO_CLI_RECORD_BYTES];
            size_t name_length = 0;
            if (!bounded_name_length(device->name, &name_length)) return GRIDPICO_INVALID;
            const char *state = connection_name(device->state);
            if (state == NULL) return GRIDPICO_INVALID;
            int size = snprintf(record, sizeof record, "DEVICE %u %s - ",
                                (unsigned)device->id, state);
            if (size < 0 || (size_t)size >= sizeof record ||
                !write_bytes(write, context, record, (size_t)size) ||
                !write_quoted_name(write, context, device->name) ||
                !write_record(write, context, "", 0)) return GRIDPICO_UNAVAILABLE;
        } else if (!format_device_record(write, context, device, color)) {
            return GRIDPICO_INVALID;
        }
        for (size_t p = 0; p < snapshot->inventory.port_count; ++p) {
            const gridpico_patch_port *port = &snapshot->inventory.ports[p];
            if (port->device == device->id &&
                !format_port_record(write, context, snapshot, port, apc)) {
                return GRIDPICO_INVALID;
            }
        }
    }
    return GRIDPICO_OK;
}

static gridpico_result parse_command(const char *line, bool *inventory)
{
    if (line == NULL || inventory == NULL) {
        return GRIDPICO_INVALID;
    }
    size_t length = 0;
    while (length < GRIDPICO_CLI_LINE_BYTES && line[length] != '\0') {
        const unsigned char c = (unsigned char)line[length];
        if (c < 0x20 || c > 0x7e) {
            return GRIDPICO_INVALID;
        }
        ++length;
    }
    if (length == GRIDPICO_CLI_LINE_BYTES) {
        return GRIDPICO_CAPACITY;
    }
    if (length == sizeof("inventory") - 1 && memcmp(line, "inventory", length) == 0) {
        *inventory = true;
        return GRIDPICO_OK;
    }
    return GRIDPICO_INVALID;
}

static bool decimal_device_id(const char *text, size_t size, gridpico_device_id *id)
{
    if (size == 0) {
        return false;
    }
    unsigned value = 0;
    for (size_t i = 0; i < size; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        const unsigned digit = (unsigned)(text[i] - '0');
        if (value > (UINT16_MAX - digit) / 10u) {
            return false;
        }
        value = value * 10u + digit;
    }
    if (value == 0) {
        return false;
    }
    *id = (gridpico_device_id)value;
    return true;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static gridpico_result parse_color(const char *text, gridpico_rgb8 *rgb)
{
    if (text == NULL || strlen(text) != 7 || text[0] != '#') {
        return GRIDPICO_INVALID;
    }
    uint8_t components[3];
    for (size_t i = 0; i < 3; ++i) {
        const int high = hex_value(text[1 + i * 2]);
        const int low = hex_value(text[2 + i * 2]);
        if (high < 0 || low < 0) {
            return GRIDPICO_INVALID;
        }
        components[i] = (uint8_t)(high * 16 + low);
    }
    *rgb = (gridpico_rgb8){components[0], components[1], components[2]};
    return GRIDPICO_OK;
}

static gridpico_result read_color(gridpico_cli_frontend *frontend,
                                  gridpico_device_id id,
                                  gridpico_device_color *color)
{
    if (frontend->read_device_color == NULL) {
        return GRIDPICO_UNAVAILABLE;
    }
    return frontend->read_device_color(frontend->session_context, id, color);
}

static gridpico_result write_color_record(gridpico_cli_write_fn write, void *context,
                                          gridpico_device_id id,
                                          const gridpico_device_color *color)
{
    char record[32];
    int size;
    if (color->is_set) {
        size = snprintf(record, sizeof record, "COLOR %u #%02X%02X%02X",
                        (unsigned)id, color->rgb.red, color->rgb.green, color->rgb.blue);
    } else {
        size = snprintf(record, sizeof record, "COLOR %u unset", (unsigned)id);
    }
    if (size < 0 || (size_t)size >= sizeof record) {
        return GRIDPICO_CAPACITY;
    }
    return write_record(write, context, record, (size_t)size) ? GRIDPICO_OK : GRIDPICO_UNAVAILABLE;
}

static gridpico_result execute_color(gridpico_cli_frontend *frontend, const char *line,
                                     gridpico_cli_write_fn write, void *context)
{
    if (strncmp(line, "color get ", 10) == 0) {
        const char *id_text = line + 10;
        gridpico_device_id id;
        if (!decimal_device_id(id_text, strlen(id_text), &id)) {
            return GRIDPICO_INVALID;
        }
        gridpico_device_color color = {0};
        const gridpico_result result = read_color(frontend, id, &color);
        return result == GRIDPICO_OK ? write_color_record(write, context, id, &color) : result;
    }
    if (strncmp(line, "color set ", 10) == 0) {
        const char *id_text = line + 10;
        const char *space = strchr(id_text, ' ');
        if (space == NULL || strchr(space + 1, ' ') != NULL) {
            return GRIDPICO_INVALID;
        }
        gridpico_device_id id;
        if (!decimal_device_id(id_text, (size_t)(space - id_text), &id)) {
            return GRIDPICO_INVALID;
        }
        gridpico_rgb8 rgb;
        gridpico_result result = parse_color(space + 1, &rgb);
        if (result != GRIDPICO_OK) {
            return result;
        }
        if (frontend->set_device_color == NULL || frontend->read_device_color == NULL) {
            return GRIDPICO_UNAVAILABLE;
        }
        result = frontend->set_device_color(frontend->session_context, id, rgb);
        if (result != GRIDPICO_OK) {
            return result;
        }
        gridpico_device_color color = {0};
        result = read_color(frontend, id, &color);
        return result == GRIDPICO_OK ? write_color_record(write, context, id, &color) : result;
    }
    return GRIDPICO_INVALID;
}

static const char *result_name(gridpico_result result)
{
    switch (result) {
    case GRIDPICO_INVALID: return "INVALID";
    case GRIDPICO_CAPACITY: return "CAPACITY";
    case GRIDPICO_UNAVAILABLE: return "UNAVAILABLE";
    case GRIDPICO_OK: return "OK";
    }
    return "INVALID";
}

static gridpico_result execute_status(gridpico_cli_frontend *frontend,
                                      gridpico_cli_write_fn write, void *context)
{
    gridpico_patch_snapshot *snapshot = &frontend->snapshot_scratch;
    const gridpico_result result = frontend->read_snapshot(frontend->session_context, snapshot);
    if (result != GRIDPICO_OK) return result;
    if (snapshot->inventory.device_count > GRIDPICO_PATCH_MAX_DEVICES ||
        snapshot->inventory.port_count > GRIDPICO_PATCH_MAX_PORTS) return GRIDPICO_CAPACITY;
    unsigned active = 0;
    unsigned pending = 0;
    gridpico_revision revision = snapshot->revision;
    if (frontend->read_apc_patch != NULL) {
        if (frontend->apc_patch_scratch == NULL) return GRIDPICO_UNAVAILABLE;
        const gridpico_result apc_result = frontend->read_apc_patch(
            frontend->session_context, frontend->apc_patch_scratch);
        if (apc_result != GRIDPICO_OK) return apc_result;
        const gridpico_apc_patch_snapshot *apc = frontend->apc_patch_scratch;
        if (apc->port_count > GRIDPICO_PATCH_MAX_PORTS) return GRIDPICO_CAPACITY;
        revision = apc->graph_revision;
        for (size_t row = 0; row < GRIDPICO_PATCH_SOURCE_ROWS; ++row) {
            for (size_t column = 0; column < GRIDPICO_PATCH_DESTINATION_COLUMNS; ++column) {
                if (apc->cells[row][column].state == GRIDPICO_PATCH_ROUTE_ACTIVE) ++active;
                else if (apc->cells[row][column].state == GRIDPICO_PATCH_ROUTE_PENDING) ++pending;
            }
        }
    } else {
        for (size_t row = 0; row < GRIDPICO_PATCH_SOURCE_ROWS; ++row) {
            for (size_t column = 0; column < GRIDPICO_PATCH_DESTINATION_COLUMNS; ++column) {
                if (snapshot->routes[row][column] == GRIDPICO_PATCH_ROUTE_ACTIVE) ++active;
                else if (snapshot->routes[row][column] == GRIDPICO_PATCH_ROUTE_PENDING) ++pending;
            }
        }
    }
    char record[GRIDPICO_CLI_RECORD_BYTES];
    const int size = snprintf(record, sizeof record, "STATUS %llu %zu %zu %u %u",
                              (unsigned long long)revision,
                              snapshot->inventory.device_count, snapshot->inventory.port_count,
                              active, pending);
    if (size < 0 || (size_t)size >= sizeof record) return GRIDPICO_CAPACITY;
    return write_record(write, context, record, (size_t)size) ? GRIDPICO_OK : GRIDPICO_UNAVAILABLE;
}

static gridpico_result execute_logs(gridpico_cli_frontend *frontend,
                                    gridpico_cli_write_fn write, void *context)
{
    if (frontend->read_logs == NULL) return GRIDPICO_UNAVAILABLE;
    gridpico_diagnostic_log_entry rows[GRIDPICO_DIAGNOSTIC_LOG_ROWS];
    const size_t count = frontend->read_logs(frontend->log_context, rows,
                                              GRIDPICO_DIAGNOSTIC_LOG_ROWS);
    if (count > GRIDPICO_DIAGNOSTIC_LOG_ROWS) return GRIDPICO_CAPACITY;
    for (size_t i = 0; i < count; ++i) {
        char record[GRIDPICO_CLI_RECORD_BYTES];
        int size = -1;
        const gridpico_diagnostic_log_entry *row = &rows[i];
        if (row->kind == GRIDPICO_DIAGNOSTIC_PORT_STATE) {
            const char *state = connection_name(row->detail.state);
            if (state == NULL) return GRIDPICO_INVALID;
            size = snprintf(record, sizeof record, "LOG %llu PORT %u %s",
                            (unsigned long long)row->sequence, (unsigned)row->port, state);
        } else if (row->kind == GRIDPICO_DIAGNOSTIC_ROUTE_ERROR ||
                   row->kind == GRIDPICO_DIAGNOSTIC_NOTE_ERROR) {
            if (row->detail.error == GRIDPICO_OK) return GRIDPICO_INVALID;
            const char *kind = row->kind == GRIDPICO_DIAGNOSTIC_ROUTE_ERROR
                                   ? "ROUTE_ERROR" : "NOTE_ERROR";
            size = snprintf(record, sizeof record, "LOG %llu %s %u %s",
                            (unsigned long long)row->sequence, kind, (unsigned)row->port,
                            result_name(row->detail.error));
        } else {
            return GRIDPICO_INVALID;
        }
        if (size < 0 || (size_t)size >= sizeof record ||
            !write_record(write, context, record, (size_t)size)) return GRIDPICO_CAPACITY;
    }
    return GRIDPICO_OK;
}

static const char *local_mode_name(gridpico_local_route_mode mode)
{
    switch (mode) {
    case GRIDPICO_LOCAL_MANAGED: return "MANAGED";
    case GRIDPICO_LOCAL_RAW: return "RAW";
    }
    return NULL;
}

static const char *local_route_state_name(gridpico_patch_route_state state)
{
    switch (state) {
    case GRIDPICO_PATCH_ROUTE_OFF: return "OFF";
    case GRIDPICO_PATCH_ROUTE_PENDING: return "PENDING";
    case GRIDPICO_PATCH_ROUTE_ACTIVE: return "ACTIVE";
    }
    return NULL;
}

static bool parse_local_route_command(const char *line, gridpico_local_route *route,
                                     bool *has_enabled, bool *enabled)
{
    char command[GRIDPICO_CLI_LINE_BYTES];
    const size_t length = strlen(line);
    if (length >= sizeof command) return false;
    memcpy(command, line, length + 1);

    char *tokens[5];
    size_t count = 0;
    char *cursor = command;
    while (*cursor != '\0') {
        if (*cursor == ' ' || count == sizeof tokens / sizeof tokens[0]) return false;
        tokens[count++] = cursor;
        char *space = strchr(cursor, ' ');
        if (space == NULL) break;
        *space = '\0';
        cursor = space + 1;
        if (*cursor == '\0' || *cursor == ' ') return false;
    }
    if ((count != 4 && count != 5) || strcmp(tokens[0], "route") != 0) return false;

    gridpico_device_id source;
    gridpico_device_id destination;
    if (!decimal_device_id(tokens[1], strlen(tokens[1]), &source) ||
        !decimal_device_id(tokens[2], strlen(tokens[2]), &destination)) {
        return false;
    }
    if (strcmp(tokens[3], "managed") == 0) route->mode = GRIDPICO_LOCAL_MANAGED;
    else if (strcmp(tokens[3], "raw") == 0) route->mode = GRIDPICO_LOCAL_RAW;
    else return false;
    route->source = source;
    route->destination = destination;

    *has_enabled = count == 5;
    if (count == 5) {
        if (strcmp(tokens[4], "on") == 0) *enabled = true;
        else if (strcmp(tokens[4], "off") == 0) *enabled = false;
        else return false;
    }
    return true;
}

static gridpico_result write_local_route_record(gridpico_cli_write_fn write, void *context,
                                                const gridpico_local_route *route,
                                                const gridpico_local_route_status *status)
{
    const char *mode = local_mode_name(route->mode);
    const char *state = local_route_state_name(status->state);
    if (mode == NULL || state == NULL) return GRIDPICO_INVALID;

    char record[GRIDPICO_CLI_RECORD_BYTES];
    const int size = snprintf(record, sizeof record, "ROUTE %u %u %s %s",
                              (unsigned)route->source, (unsigned)route->destination,
                              mode, state);
    if (size < 0 || (size_t)size >= sizeof record) return GRIDPICO_CAPACITY;
    return write_record(write, context, record, (size_t)size)
               ? GRIDPICO_OK : GRIDPICO_UNAVAILABLE;
}

static gridpico_result execute_local_route(gridpico_cli_frontend *frontend, const char *line,
                                           gridpico_cli_write_fn write, void *context)
{
    gridpico_local_route route = {0};
    bool has_enabled = false;
    bool enabled = false;
    if (!parse_local_route_command(line, &route, &has_enabled, &enabled)) {
        return GRIDPICO_INVALID;
    }
    const bool target_callbacks = frontend->routing_read_route != NULL ||
                                  frontend->routing_set_route != NULL ||
                                  frontend->routing_read != NULL;
    const gridpico_local_read_route_fn read_route = target_callbacks
        ? frontend->routing_read_route : frontend->read_local_route;
    const gridpico_local_set_route_fn set_route = target_callbacks
        ? frontend->routing_set_route : frontend->set_local_route;
    void *callback_context = target_callbacks ? frontend->routing_context
                                              : frontend->session_context;
    if (read_route == NULL || (has_enabled && set_route == NULL))
        return GRIDPICO_UNAVAILABLE;

    if (has_enabled) {
        const gridpico_local_route_request request = {.route = route, .enabled = enabled};
        const gridpico_result result = set_route(callback_context, &request);
        if (result != GRIDPICO_OK) return result;
    }

    gridpico_local_route_status status = {0};
    const gridpico_result result = read_route(callback_context, &route, &status);
    if (result != GRIDPICO_OK) return result;
    return write_local_route_record(write, context, &route, &status);
}

static bool parse_unsigned_decimal(const char *text, size_t size, uint64_t *value)
{
    if (text == NULL || value == NULL || size == 0) return false;
    uint64_t parsed = 0;
    for (size_t i = 0; i < size; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        const uint64_t digit = (uint64_t)(text[i] - '0');
        if (parsed > (UINT64_MAX - digit) / 10u) return false;
        parsed = parsed * 10u + digit;
    }
    *value = parsed;
    return true;
}

typedef struct {
    gridpico_local_route route;
    bool enabled;
    gridpico_revision revision;
} guarded_route_command;

static bool parse_guarded_route_command(const char *line, guarded_route_command *command)
{
    if (line == NULL || command == NULL) return false;
    const size_t length = strlen(line);
    if (length == 0 || length >= GRIDPICO_CLI_LINE_BYTES) return false;
    char buffer[GRIDPICO_CLI_LINE_BYTES];
    memcpy(buffer, line, length + 1);
    char *tokens[7];
    size_t count = 0;
    char *cursor = buffer;
    while (*cursor != '\0') {
        if (*cursor == ' ' || count == sizeof tokens / sizeof tokens[0]) return false;
        tokens[count++] = cursor;
        char *space = strchr(cursor, ' ');
        if (space == NULL) break;
        *space = '\0';
        cursor = space + 1;
        if (*cursor == '\0' || *cursor == ' ') return false;
    }
    if (count != 7 || strcmp(tokens[0], "route") != 0 ||
        strcmp(tokens[3], "managed") != 0 || strcmp(tokens[5], "if") != 0) return false;

    gridpico_device_id source;
    gridpico_device_id destination;
    if (!decimal_device_id(tokens[1], strlen(tokens[1]), &source) ||
        !decimal_device_id(tokens[2], strlen(tokens[2]), &destination)) return false;
    if (strcmp(tokens[4], "on") == 0) command->enabled = true;
    else if (strcmp(tokens[4], "off") == 0) command->enabled = false;
    else return false;

    uint64_t revision;
    if (!parse_unsigned_decimal(tokens[6], strlen(tokens[6]), &revision) || revision == 0)
        return false;
    command->route = (gridpico_local_route){
        .source = source,
        .destination = destination,
        .mode = GRIDPICO_LOCAL_MANAGED,
    };
    command->revision = (gridpico_revision)revision;
    return true;
}

static bool parse_routing_command(const char *line, bool *continuation,
                                  gridpico_revision *revision, size_t *offset)
{
    if (strcmp(line, "routing") == 0) {
        *continuation = false;
        *revision = 0;
        *offset = 0;
        return true;
    }
    if (strncmp(line, "routing ", sizeof("routing ") - 1) != 0) return false;

    const char *revision_text = line + sizeof("routing ") - 1;
    const char *separator = strchr(revision_text, ' ');
    if (separator == NULL || separator == revision_text || separator[1] == '\0' ||
        separator[1] == ' ' || strchr(separator + 1, ' ') != NULL) return false;
    uint64_t parsed_revision;
    uint64_t parsed_offset;
    if (!parse_unsigned_decimal(revision_text, (size_t)(separator - revision_text),
                                &parsed_revision) || parsed_revision == 0 ||
        !parse_unsigned_decimal(separator + 1, strlen(separator + 1), &parsed_offset) ||
        parsed_offset > SIZE_MAX) return false;
    *continuation = true;
    *revision = (gridpico_revision)parsed_revision;
    *offset = (size_t)parsed_offset;
    return true;
}

static gridpico_result validate_local_routing_snapshot(
    const gridpico_local_routing_snapshot *snapshot)
{
    const gridpico_patch_inventory *inventory = &snapshot->graph.registry.inventory;
    if (inventory->device_count > GRIDPICO_PATCH_MAX_DEVICES ||
        inventory->port_count > GRIDPICO_PATCH_MAX_PORTS ||
        snapshot->graph.route_count > GRIDPICO_PATCH_MAX_BRANCHES) return GRIDPICO_CAPACITY;
    if (snapshot->revision == 0) return GRIDPICO_INVALID;

    for (size_t i = 0; i < inventory->device_count; ++i) {
        size_t name_length;
        if (inventory->devices[i].id == 0 ||
            !bounded_name_length(inventory->devices[i].name, &name_length) ||
            connection_name(inventory->devices[i].state) == NULL) return GRIDPICO_INVALID;
        for (size_t j = 0; j < i; ++j) {
            if (inventory->devices[j].id == inventory->devices[i].id) return GRIDPICO_INVALID;
        }
    }
    for (size_t i = 0; i < inventory->port_count; ++i) {
        const gridpico_patch_port *port = &inventory->ports[i];
        size_t name_length;
        if (port->id == 0 || !bounded_name_length(port->name, &name_length) ||
            connection_name(port->state) == NULL ||
            (port->direction != GRIDPICO_PATCH_INPUT &&
             port->direction != GRIDPICO_PATCH_OUTPUT) ||
            snapshot->graph.registry.port_capabilities[i] < GRIDPICO_LOCAL_PORT_MANAGED ||
            snapshot->graph.registry.port_capabilities[i] > GRIDPICO_LOCAL_PORT_PANEL ||
            snapshot->availability[i] < GRIDPICO_LOCAL_UNAVAILABLE ||
            snapshot->availability[i] > GRIDPICO_LOCAL_IN_USE ||
            find_device_index(inventory, port->device) < 0) return GRIDPICO_INVALID;
        for (size_t j = 0; j < i; ++j) {
            if (inventory->ports[j].id == port->id) return GRIDPICO_INVALID;
        }
    }
    for (size_t i = 0; i < snapshot->graph.route_count; ++i) {
        const gridpico_local_enabled_route *route = &snapshot->graph.routes[i];
        if (route->state != GRIDPICO_PATCH_ROUTE_PENDING &&
            route->state != GRIDPICO_PATCH_ROUTE_ACTIVE) return GRIDPICO_INVALID;
        if (route->route.mode != GRIDPICO_LOCAL_MANAGED &&
            route->route.mode != GRIDPICO_LOCAL_RAW) return GRIDPICO_INVALID;
    }
    return GRIDPICO_OK;
}

static int compare_local_routes(const gridpico_local_enabled_route *left,
                                const gridpico_local_enabled_route *right)
{
    if (left->route.source != right->route.source)
        return left->route.source < right->route.source ? -1 : 1;
    if (left->route.destination != right->route.destination)
        return left->route.destination < right->route.destination ? -1 : 1;
    if (left->route.mode != right->route.mode)
        return left->route.mode < right->route.mode ? -1 : 1;
    return 0;
}

static bool compatible_enabled_route(const gridpico_local_routing_snapshot *snapshot,
                                     const gridpico_local_route *route)
{
    const gridpico_patch_inventory *inventory = &snapshot->graph.registry.inventory;
    const gridpico_patch_port *source = NULL;
    const gridpico_patch_port *destination = NULL;
    size_t source_index = 0;
    size_t destination_index = 0;
    for (size_t i = 0; i < inventory->port_count; ++i) {
        if (inventory->ports[i].id == route->source) {
            source = &inventory->ports[i];
            source_index = i;
        }
        if (inventory->ports[i].id == route->destination) {
            destination = &inventory->ports[i];
            destination_index = i;
        }
    }
    if (source == NULL || destination == NULL ||
        source->direction != GRIDPICO_PATCH_INPUT ||
        destination->direction != GRIDPICO_PATCH_OUTPUT) return false;
    const gridpico_local_port_capability expected = route->mode == GRIDPICO_LOCAL_MANAGED
        ? GRIDPICO_LOCAL_PORT_MANAGED : GRIDPICO_LOCAL_PORT_RAW;
    return snapshot->graph.registry.port_capabilities[source_index] == expected &&
           snapshot->graph.registry.port_capabilities[destination_index] == expected;
}

static gridpico_result execute_guarded_route(gridpico_cli_frontend *frontend, const char *line,
                                            gridpico_cli_write_fn write, void *context)
{
    guarded_route_command command;
    if (!parse_guarded_route_command(line, &command)) return GRIDPICO_INVALID;
    if (frontend->routing_read == NULL || frontend->routing_read_route == NULL ||
        frontend->routing_set_route == NULL || frontend->routing_scratch == NULL)
        return GRIDPICO_UNAVAILABLE;

    gridpico_local_routing_snapshot *snapshot = frontend->routing_scratch;
    const gridpico_result result = frontend->routing_read(frontend->routing_context, snapshot);
    if (result != GRIDPICO_OK) return result;
    const gridpico_result validation = validate_local_routing_snapshot(snapshot);
    if (validation != GRIDPICO_OK) return validation;
    if (snapshot->revision != command.revision) return GRIDPICO_UNAVAILABLE;
    if (!compatible_enabled_route(snapshot, &command.route)) return GRIDPICO_INVALID;

    const gridpico_local_route_request request = {
        .route = command.route,
        .enabled = command.enabled,
    };
    const gridpico_result set_result = frontend->routing_set_route(
        frontend->routing_context, &request);
    if (set_result != GRIDPICO_OK) return set_result;

    gridpico_local_route_status status = {0};
    const gridpico_result read_result = frontend->routing_read_route(
        frontend->routing_context, &command.route, &status);
    if (read_result != GRIDPICO_OK) return read_result;
    return write_local_route_record(write, context, &command.route, &status);
}

typedef struct {
    char bytes[GRIDPICO_LOCAL_ROUTING_RECORD_BYTES];
    size_t size;
    bool valid;
} bounded_routing_record;

static void routing_record_append(void *context, const char *bytes, size_t size)
{
    bounded_routing_record *record = context;
    if (!record->valid || size > sizeof record->bytes - record->size) {
        record->valid = false;
        return;
    }
    memcpy(record->bytes + record->size, bytes, size);
    record->size += size;
}

static gridpico_result write_routing_named_record(gridpico_cli_write_fn write, void *context,
                                                  const char *prefix,
                                                  const char name[GRIDPICO_PATCH_NAME_BYTES])
{
    bounded_routing_record record = {.valid = true};
    const int size = snprintf(record.bytes, sizeof record.bytes - 2, "%s", prefix);
    if (size < 0 || (size_t)size >= sizeof record.bytes - 2) return GRIDPICO_CAPACITY;
    record.size = (size_t)size;
    if (!record.valid || !write_quoted_name(routing_record_append, &record, name) ||
        !record.valid) return GRIDPICO_CAPACITY;
    routing_record_append(&record, "\r\n", 2);
    if (!record.valid || !write_bytes(write, context, record.bytes, record.size))
        return GRIDPICO_UNAVAILABLE;
    return GRIDPICO_OK;
}

static const char *routing_capability_name(gridpico_local_port_capability capability)
{
    switch (capability) {
    case GRIDPICO_LOCAL_PORT_MANAGED: return "managed";
    case GRIDPICO_LOCAL_PORT_RAW: return "raw";
    case GRIDPICO_LOCAL_PORT_PANEL: return "panel";
    }
    return NULL;
}

static const char *routing_availability_name(gridpico_local_port_availability availability)
{
    switch (availability) {
    case GRIDPICO_LOCAL_FREE: return "free";
    case GRIDPICO_LOCAL_IN_USE: return "in-use";
    case GRIDPICO_LOCAL_UNAVAILABLE: return "unavailable";
    }
    return NULL;
}

static gridpico_result write_routing_device(gridpico_cli_write_fn write, void *context,
                                            const gridpico_local_routing_snapshot *snapshot,
                                            size_t index)
{
    const gridpico_patch_device *device = &snapshot->graph.registry.inventory.devices[index];
    const gridpico_device_color *color = &snapshot->colors[index];
    char prefix[80];
    if (color->is_set) {
        const int size = snprintf(prefix, sizeof prefix, "DEVICE %u %s #%02X%02X%02X ",
                                  (unsigned)device->id, connection_name(device->state),
                                  color->rgb.red, color->rgb.green, color->rgb.blue);
        if (size < 0 || (size_t)size >= sizeof prefix) return GRIDPICO_CAPACITY;
    } else {
        const int size = snprintf(prefix, sizeof prefix, "DEVICE %u %s unset ",
                                  (unsigned)device->id, connection_name(device->state));
        if (size < 0 || (size_t)size >= sizeof prefix) return GRIDPICO_CAPACITY;
    }
    return write_routing_named_record(write, context, prefix, device->name);
}

static gridpico_result write_routing_port(gridpico_cli_write_fn write, void *context,
                                          const gridpico_local_routing_snapshot *snapshot,
                                          size_t index)
{
    const gridpico_patch_port *port = &snapshot->graph.registry.inventory.ports[index];
    const char *direction = port->direction == GRIDPICO_PATCH_INPUT ? "in" : "out";
    const char *capability = routing_capability_name(
        snapshot->graph.registry.port_capabilities[index]);
    const char *availability = routing_availability_name(snapshot->availability[index]);
    if (capability == NULL || availability == NULL) return GRIDPICO_INVALID;
    char prefix[96];
    const int size = snprintf(prefix, sizeof prefix, "BINDING %u %u %s %s %s ",
                              (unsigned)port->id, (unsigned)port->device, direction,
                              capability, availability);
    if (size < 0 || (size_t)size >= sizeof prefix) return GRIDPICO_CAPACITY;
    return write_routing_named_record(write, context, prefix, port->name);
}

static gridpico_result write_routing_route(gridpico_cli_write_fn write, void *context,
                                           const gridpico_local_enabled_route *route)
{
    const char *mode = local_mode_name(route->route.mode);
    const char *state = local_route_state_name(route->state);
    if (mode == NULL || state == NULL) return GRIDPICO_INVALID;
    char record[GRIDPICO_LOCAL_ROUTING_RECORD_BYTES + 1];
    const int size = snprintf(record, sizeof record, "ROUTE %u %u %s %s\r\n",
                              (unsigned)route->route.source,
                              (unsigned)route->route.destination, mode, state);
    if (size < 0 || (size_t)size > GRIDPICO_LOCAL_ROUTING_RECORD_BYTES)
        return GRIDPICO_CAPACITY;
    return write_bytes(write, context, record, (size_t)size)
               ? GRIDPICO_OK : GRIDPICO_UNAVAILABLE;
}

static gridpico_result write_routing_header(gridpico_cli_write_fn write, void *context,
                                            gridpico_revision revision, size_t next,
                                            bool at_end)
{
    char record[64];
    const int size = at_end
        ? snprintf(record, sizeof record, "ROUTING %llu -\r\n", (unsigned long long)revision)
        : snprintf(record, sizeof record, "ROUTING %llu %zu\r\n",
                   (unsigned long long)revision, next);
    if (size < 0 || (size_t)size >= sizeof record) return GRIDPICO_CAPACITY;
    return write_bytes(write, context, record, (size_t)size)
               ? GRIDPICO_OK : GRIDPICO_UNAVAILABLE;
}

static gridpico_result write_routing_page(const gridpico_local_routing_snapshot *snapshot,
                                          size_t offset, gridpico_cli_write_fn write,
                                          void *context)
{
    const gridpico_result validation = validate_local_routing_snapshot(snapshot);
    if (validation != GRIDPICO_OK) return validation;

    const gridpico_patch_inventory *inventory = &snapshot->graph.registry.inventory;
    const size_t total = inventory->device_count + inventory->port_count +
                         snapshot->graph.route_count;
    if (offset > total) return GRIDPICO_INVALID;

    const gridpico_local_enabled_route *routes[GRIDPICO_PATCH_MAX_BRANCHES];
    for (size_t i = 0; i < snapshot->graph.route_count; ++i) {
        routes[i] = &snapshot->graph.routes[i];
        if (!compatible_enabled_route(snapshot, &routes[i]->route)) return GRIDPICO_INVALID;
    }
    for (size_t i = 1; i < snapshot->graph.route_count; ++i) {
        const gridpico_local_enabled_route *route = routes[i];
        size_t j = i;
        while (j > 0 && compare_local_routes(route, routes[j - 1]) < 0) {
            routes[j] = routes[j - 1];
            --j;
        }
        routes[j] = route;
    }
    for (size_t i = 1; i < snapshot->graph.route_count; ++i) {
        if (compare_local_routes(routes[i - 1], routes[i]) == 0) return GRIDPICO_INVALID;
    }

    const size_t count = total - offset < GRIDPICO_LOCAL_ROUTING_PAGE_RECORDS
                             ? total - offset : GRIDPICO_LOCAL_ROUTING_PAGE_RECORDS;
    const size_t next = offset + count;
    gridpico_result result = write_routing_header(write, context, snapshot->revision,
                                                  next, next == total);
    if (result != GRIDPICO_OK) return result;
    for (size_t i = offset; i < next; ++i) {
        if (i < inventory->device_count) {
            result = write_routing_device(write, context, snapshot, i);
        } else if (i < inventory->device_count + inventory->port_count) {
            result = write_routing_port(write, context, snapshot,
                                        i - inventory->device_count);
        } else {
            result = write_routing_route(write, context,
                                         routes[i - inventory->device_count -
                                                inventory->port_count]);
        }
        if (result != GRIDPICO_OK) return result;
    }
    return GRIDPICO_OK;
}

static gridpico_result execute_routing_target(gridpico_cli_frontend *frontend,
                                              bool continuation,
                                              gridpico_revision requested_revision,
                                              size_t offset,
                                              gridpico_cli_write_fn write, void *context)
{
    if (frontend->routing_read == NULL || frontend->routing_scratch == NULL)
        return GRIDPICO_UNAVAILABLE;
    gridpico_local_routing_snapshot *snapshot = frontend->routing_scratch;
    const gridpico_result read_result = frontend->routing_read(frontend->routing_context,
                                                               snapshot);
    if (read_result != GRIDPICO_OK) return read_result;
    if (continuation && snapshot->revision != requested_revision) return GRIDPICO_UNAVAILABLE;
    if (offset > GRIDPICO_PATCH_MAX_DEVICES + GRIDPICO_PATCH_MAX_PORTS +
                     GRIDPICO_PATCH_MAX_BRANCHES) return GRIDPICO_INVALID;
    return write_routing_page(snapshot, offset, write, context);
}

static gridpico_result execute_routing_legacy(gridpico_cli_frontend *frontend,
                                              bool continuation,
                                              gridpico_revision requested_revision,
                                              size_t offset,
                                              gridpico_cli_write_fn write, void *context)
{
    if (frontend->read_local_routing == NULL) return GRIDPICO_UNAVAILABLE;
    gridpico_local_routing_snapshot snapshot = {0};
    const gridpico_result read_result = frontend->read_local_routing(
        frontend->session_context, &snapshot);
    if (read_result != GRIDPICO_OK) return read_result;
    if (continuation && snapshot.revision != requested_revision) return GRIDPICO_UNAVAILABLE;
    if (offset > GRIDPICO_PATCH_MAX_DEVICES + GRIDPICO_PATCH_MAX_PORTS +
                     GRIDPICO_PATCH_MAX_BRANCHES) return GRIDPICO_INVALID;
    return write_routing_page(&snapshot, offset, write, context);
}

static gridpico_result execute_routing(gridpico_cli_frontend *frontend, const char *line,
                                       gridpico_cli_write_fn write, void *context)
{
    bool continuation;
    gridpico_revision requested_revision;
    size_t offset;
    if (!parse_routing_command(line, &continuation, &requested_revision, &offset))
        return GRIDPICO_INVALID;
    if (frontend->routing_read != NULL)
        return execute_routing_target(frontend, continuation, requested_revision, offset,
                                      write, context);
    return execute_routing_legacy(frontend, continuation, requested_revision, offset,
                                  write, context);
}

static const char *error_record(gridpico_result result)
{
    switch (result) {
    case GRIDPICO_INVALID:
        return "ERR INVALID";
    case GRIDPICO_CAPACITY:
        return "ERR CAPACITY";
    case GRIDPICO_UNAVAILABLE:
        return "ERR UNAVAILABLE";
    case GRIDPICO_OK:
        break;
    }
    return "ERR INVALID";
}

static void terminal_response(void *context, const char *bytes, size_t size);

static void terminal_application_command(EmbeddedCli *cli, char *args, void *context)
{
    gridpico_cli_frontend *frontend = cli->appContext;
    const cli_help_entry *entry = context;
    char line[GRIDPICO_CLI_LINE_BYTES];
    const size_t name_size = strlen(entry->name);
    const size_t args_size = args == NULL ? 0 : strlen(args);
    if (name_size + (args_size == 0 ? 0 : args_size + 1) >= sizeof line) {
        (void)write_record(terminal_response, cli, "ERR CAPACITY", sizeof("ERR CAPACITY") - 1);
        return;
    }
    memcpy(line, entry->name, name_size);
    size_t offset = name_size;
    if (args_size != 0) {
        line[offset++] = ' ';
        memcpy(line + offset, args, args_size);
        offset += args_size;
    }
    line[offset] = '\0';
    (void)gridpico_cli_execute_line(frontend, line, terminal_response, cli);
}

static bool register_help_bindings(gridpico_cli_frontend *frontend)
{
    const cli_help_entry *entries[10];
    const size_t count = help_entries(frontend, entries, sizeof entries / sizeof entries[0]);
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(entries[i]->name, "help") == 0) continue;
        if (!embeddedCliAddBinding(frontend->terminal, (CliCommandBinding){
                .name = entries[i]->name,
                .help = entries[i]->detail,
                .tokenizeArgs = false,
                .context = (void *)entries[i],
                .binding = terminal_application_command,
            })) {
            return false;
        }
    }
    /* Keep embedded-cli's parser/editor, but route its built-in help command
     * through the same executor and metadata as Diagnostics. */
    EmbeddedCliImpl *implementation = (EmbeddedCliImpl *)frontend->terminal->_impl;
    for (uint16_t i = 0; i < implementation->bindingsCount; ++i) {
        CliCommandBinding *binding = &implementation->bindings[i];
        if (strcmp(binding->name, "help") == 0) {
            binding->help = help_self.detail;
            binding->tokenizeArgs = false;
            binding->context = (void *)&help_self;
            binding->binding = terminal_application_command;
            return true;
        }
    }
    return false;
}

gridpico_result gridpico_cli_execute_line(gridpico_cli_frontend *frontend,
                                         const char *line,
                                         gridpico_cli_write_fn write,
                                         void *write_context)
{
    if (frontend == NULL || frontend->read_snapshot == NULL || write == NULL || line == NULL) {
        return GRIDPICO_INVALID;
    }
    bool asynchronous = false;
    bool inventory = false;
    gridpico_result result = parse_command(line, &inventory);
    if (result == GRIDPICO_OK && inventory) {
        result = write_inventory(frontend, write, write_context);
    } else if (result == GRIDPICO_INVALID) {
        const cli_help_entry *entry = command_for_line(line);
        if (entry == NULL) {
            result = GRIDPICO_INVALID;
        } else {
            switch (entry->command) {
            case CLI_COMMAND_HELP: {
                const char *topic = strcmp(line, "help") == 0 ? NULL : line + 5;
                if (topic != NULL && (*topic == '\0' ||
                    (strchr(topic, ' ') != NULL && strcmp(topic, "color get") != 0 &&
                     strcmp(topic, "color set") != 0))) {
                    result = GRIDPICO_INVALID;
                } else {
                    result = execute_help(frontend, topic, write, write_context);
                }
                break;
            }
            case CLI_COMMAND_INVENTORY:
                result = strcmp(line, "inventory") == 0
                             ? write_inventory(frontend, write, write_context)
                             : GRIDPICO_INVALID;
                break;
            case CLI_COMMAND_STATUS:
                result = strcmp(line, "status") == 0
                             ? execute_status(frontend, write, write_context)
                             : GRIDPICO_INVALID;
                break;
            case CLI_COMMAND_LOGS:
                result = strcmp(line, "logs") == 0
                             ? execute_logs(frontend, write, write_context)
                             : GRIDPICO_INVALID;
                break;
            case CLI_COMMAND_COLOR:
                result = strncmp(line, "color ", 6) == 0
                             ? execute_color(frontend, line, write, write_context)
                             : GRIDPICO_INVALID;
                break;
            case CLI_COMMAND_ROUTE:
                if (strncmp(line, "route ", 6) != 0) {
                    result = GRIDPICO_INVALID;
                } else if (strstr(line, " if ") != NULL) {
                    result = execute_guarded_route(frontend, line, write, write_context);
                } else {
                    result = execute_local_route(frontend, line, write, write_context);
                }
                break;
            case CLI_COMMAND_ROUTING:
                result = strcmp(line, "routing") == 0 || strncmp(line, "routing ", 8) == 0
                             ? execute_routing(frontend, line, write, write_context)
                             : GRIDPICO_INVALID;
                break;
            case CLI_COMMAND_TRACE:
            case CLI_COMMAND_HUBTRACE: {
                const unsigned capability = entry->command == CLI_COMMAND_TRACE
                                                ? GRIDPICO_CLI_PLATFORM_TRACE
                                                : GRIDPICO_CLI_PLATFORM_HUBTRACE;
                const bool exact_form = entry->command == CLI_COMMAND_TRACE
                    ? (strcmp(line, "trace") == 0 || strcmp(line, "TRACE") == 0)
                    : (strcmp(line, "hubtrace") == 0 || strcmp(line, "HUBTRACE") == 0);
                result = !exact_form ? GRIDPICO_INVALID :
                         frontend->start_platform == NULL ||
                                 (frontend->platform_capabilities & capability) == 0
                             ? GRIDPICO_UNAVAILABLE
                             : frontend->start_platform(frontend->platform_context, capability);
                asynchronous = result == GRIDPICO_OK;
                break;
            }
            case CLI_COMMAND_BOOTSEL:
                /* Recovery is recognized only by the platform raw-byte parser. */
                result = GRIDPICO_INVALID;
                break;
            }
        }
    }
    if (!asynchronous) {
        (void)write_record(write, write_context,
                           result == GRIDPICO_OK ? "OK" : error_record(result),
                           strlen(result == GRIDPICO_OK ? "OK" : error_record(result)));
    }
    return result;
}

static void terminal_write_char(EmbeddedCli *cli, char c)
{
    gridpico_cli_frontend *frontend = cli->appContext;
    if (frontend->terminal_write != NULL) {
        frontend->terminal_write(frontend->terminal_write_context, &c, 1);
    }
}

static void terminal_response(void *context, const char *bytes, size_t size)
{
    EmbeddedCli *cli = context;
    for (size_t i = 0; i < size; ++i) {
        terminal_write_char(cli, bytes[i]);
    }
}

static void terminal_command(EmbeddedCli *cli, CliCommand *command)
{
    gridpico_cli_frontend *frontend = cli->appContext;
    char line[GRIDPICO_CLI_LINE_BYTES];
    const size_t name_size = strlen(command->name);
    const size_t args_size = command->args == NULL ? 0 : strlen(command->args);
    if (name_size + (args_size == 0 ? 0 : args_size + 1) >= sizeof line) {
        (void)write_record(terminal_response, cli, "ERR CAPACITY", sizeof("ERR CAPACITY") - 1);
        return;
    }
    memcpy(line, command->name, name_size);
    size_t offset = name_size;
    if (args_size != 0) {
        line[offset++] = ' ';
        memcpy(line + offset, command->args, args_size);
        offset += args_size;
    }
    line[offset] = '\0';
    (void)gridpico_cli_execute_line(frontend, line, terminal_response, cli);
}

gridpico_result gridpico_cli_init(gridpico_cli_frontend *frontend,
                                 void *session_context,
                                 gridpico_patch_read_snapshot_fn read_snapshot,
                                 gridpico_patch_read_device_color_fn read_device_color,
                                 gridpico_patch_set_device_color_fn set_device_color,
                                 void *log_context,
                                 gridpico_read_diagnostic_log_fn read_logs,
                                 gridpico_cli_write_fn terminal_write,
                                 void *terminal_write_context)
{
    if (frontend == NULL || read_snapshot == NULL || terminal_write == NULL) {
        return GRIDPICO_INVALID;
    }
    memset(frontend, 0, sizeof *frontend);
    frontend->session_context = session_context;
    frontend->read_snapshot = read_snapshot;
    frontend->read_device_color = read_device_color;
    frontend->set_device_color = set_device_color;
    frontend->log_context = log_context;
    frontend->read_logs = read_logs;
    frontend->terminal_write = terminal_write;
    frontend->terminal_write_context = terminal_write_context;
    EmbeddedCliConfig config = *embeddedCliDefaultConfig();
    config.invitation = "> ";
    config.rxBufferSize = GRIDPICO_CLI_RX_BYTES;
    /* embedded-cli reserves two bytes in the command buffer. */
    config.cmdBufferSize = GRIDPICO_CLI_LINE_BYTES + 2;
    config.historyBufferSize = GRIDPICO_CLI_HISTORY_BYTES;
    config.maxBindingCount = 10;
    config.cliBuffer = frontend->library_storage;
    config.cliBufferSize = GRIDPICO_CLI_LIBRARY_BYTES;
    config.enableAutoComplete = false;
    if (embeddedCliRequiredSize(&config) > GRIDPICO_CLI_LIBRARY_BYTES) {
        return GRIDPICO_CAPACITY;
    }
    frontend->terminal = embeddedCliNew(&config);
    if (frontend->terminal == NULL) {
        return GRIDPICO_CAPACITY;
    }
    frontend->terminal->appContext = frontend;
    frontend->terminal->writeChar = terminal_write_char;
    frontend->terminal->onCommand = terminal_command;
    return GRIDPICO_OK;
}

void gridpico_cli_bind_platform(gridpico_cli_frontend *frontend,
                                unsigned capabilities,
                                gridpico_cli_start_platform_fn start,
                                void *context)
{
    if (frontend == NULL) return;
    const unsigned trace_capabilities = GRIDPICO_CLI_PLATFORM_TRACE |
                                        GRIDPICO_CLI_PLATFORM_HUBTRACE;
    if (start == NULL) capabilities &= ~trace_capabilities;
    frontend->platform_capabilities = capabilities;
    frontend->start_platform = start;
    frontend->platform_context = context;
}

void gridpico_cli_bind_apc_patch(gridpico_cli_frontend *frontend,
                                 gridpico_apc_patch_read_fn read_apc_patch,
                                 gridpico_apc_patch_snapshot *scratch)
{
    if (frontend == NULL) return;
    frontend->read_apc_patch = read_apc_patch;
    frontend->apc_patch_scratch = read_apc_patch == NULL ? NULL : scratch;
}

void gridpico_cli_bind_routing(gridpico_cli_frontend *frontend, void *context,
                              gridpico_local_read_routing_fn read_routing,
                              gridpico_local_read_route_fn read_route,
                              gridpico_local_set_route_fn set_route,
                              gridpico_local_routing_snapshot *scratch)
{
    if (frontend == NULL) return;
    frontend->routing_context = context;
    frontend->routing_read = read_routing;
    frontend->routing_read_route = read_route;
    frontend->routing_set_route = set_route;
    frontend->routing_scratch = read_routing == NULL ? NULL : scratch;
}

void gridpico_cli_receive(gridpico_cli_frontend *frontend, const char *bytes, size_t size)
{
    if (frontend == NULL || frontend->terminal == NULL || (bytes == NULL && size != 0)) {
        return;
    }
    for (size_t i = 0; i < size; ++i) {
        embeddedCliReceiveChar(frontend->terminal, bytes[i]);
    }
}

void gridpico_cli_process(gridpico_cli_frontend *frontend)
{
    if (frontend != NULL && frontend->terminal != NULL) {
        if (!frontend->help_bindings_registered) {
            if (!register_help_bindings(frontend)) {
                (void)write_record(terminal_response, frontend->terminal,
                                   "ERR CAPACITY", sizeof("ERR CAPACITY") - 1);
                return;
            }
            frontend->help_bindings_registered = true;
        }
        embeddedCliProcess(frontend->terminal);
    }
}
