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
                               const gridpico_patch_port *port)
{
    char record[GRIDPICO_CLI_RECORD_BYTES];
    char assignment[24];
    const char *state = connection_name(port->state);
    const char *direction = port->direction == GRIDPICO_PATCH_INPUT ? "in" : "out";
    if (state == NULL || port->direction > GRIDPICO_PATCH_OUTPUT ||
        !port_assignment(snapshot, port, assignment, sizeof assignment)) {
        return false;
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
    gridpico_patch_snapshot snapshot;
    const gridpico_result result = frontend->read_snapshot(frontend->session_context, &snapshot);
    if (result != GRIDPICO_OK) {
        return result;
    }
    if (snapshot.inventory.device_count > GRIDPICO_PATCH_MAX_DEVICES ||
        snapshot.inventory.port_count > GRIDPICO_PATCH_MAX_PORTS) {
        return GRIDPICO_CAPACITY;
    }
    for (size_t p = 0; p < snapshot.inventory.port_count; ++p) {
        if (find_device_index(&snapshot.inventory, snapshot.inventory.ports[p].device) < 0) {
            return GRIDPICO_INVALID;
        }
    }
    if (frontend->read_device_color == NULL) {
        return GRIDPICO_UNAVAILABLE;
    }
    gridpico_device_color colors[GRIDPICO_PATCH_MAX_DEVICES] = {{0}};
    for (size_t i = 0; i < snapshot.inventory.device_count; ++i) {
        const gridpico_result color_result = frontend->read_device_color(
            frontend->session_context, snapshot.inventory.devices[i].id, &colors[i]);
        if (color_result != GRIDPICO_OK) {
            return color_result;
        }
    }
    for (size_t i = 0; i < snapshot.inventory.device_count; ++i) {
        const gridpico_patch_device *device = &snapshot.inventory.devices[i];
        if (!format_device_record(write, context, device, &colors[i])) {
            return GRIDPICO_INVALID;
        }
        for (size_t p = 0; p < snapshot.inventory.port_count; ++p) {
            const gridpico_patch_port *port = &snapshot.inventory.ports[p];
            if (port->device == device->id && !format_port_record(write, context, &snapshot, port)) {
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
    gridpico_patch_snapshot snapshot;
    const gridpico_result result = frontend->read_snapshot(frontend->session_context, &snapshot);
    if (result != GRIDPICO_OK) return result;
    if (snapshot.inventory.device_count > GRIDPICO_PATCH_MAX_DEVICES ||
        snapshot.inventory.port_count > GRIDPICO_PATCH_MAX_PORTS) return GRIDPICO_CAPACITY;
    unsigned active = 0;
    unsigned pending = 0;
    for (size_t row = 0; row < GRIDPICO_PATCH_SOURCE_ROWS; ++row) {
        for (size_t column = 0; column < GRIDPICO_PATCH_DESTINATION_COLUMNS; ++column) {
            if (snapshot.routes[row][column] == GRIDPICO_PATCH_ROUTE_ACTIVE) ++active;
            else if (snapshot.routes[row][column] == GRIDPICO_PATCH_ROUTE_PENDING) ++pending;
        }
    }
    char record[GRIDPICO_CLI_RECORD_BYTES];
    const int size = snprintf(record, sizeof record, "STATUS %llu %zu %zu %u %u",
                              (unsigned long long)snapshot.revision,
                              snapshot.inventory.device_count, snapshot.inventory.port_count,
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

    if (has_enabled) {
        const gridpico_local_route_request request = {.route = route, .enabled = enabled};
        const gridpico_result result = frontend->set_local_route(frontend->session_context,
                                                                 &request);
        if (result != GRIDPICO_OK) return result;
    }

    gridpico_local_route_status status = {0};
    const gridpico_result result = frontend->read_local_route(frontend->session_context,
                                                              &route, &status);
    if (result != GRIDPICO_OK) return result;
    return write_local_route_record(write, context, &route, &status);
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

gridpico_result gridpico_cli_execute_line(gridpico_cli_frontend *frontend,
                                         const char *line,
                                         gridpico_cli_write_fn write,
                                         void *write_context)
{
    if (frontend == NULL || frontend->read_snapshot == NULL || write == NULL || line == NULL) {
        return GRIDPICO_INVALID;
    }
    bool inventory = false;
    gridpico_result result = parse_command(line, &inventory);
    if (result == GRIDPICO_OK && inventory) result = write_inventory(frontend, write, write_context);
    else if (result == GRIDPICO_INVALID && strcmp(line, "status") == 0)
        result = execute_status(frontend, write, write_context);
    else if (result == GRIDPICO_INVALID && strcmp(line, "logs") == 0)
        result = execute_logs(frontend, write, write_context);
    else if (result == GRIDPICO_INVALID && strncmp(line, "color ", 6) == 0)
        result = execute_color(frontend, line, write, write_context);
    else if (result == GRIDPICO_INVALID && strncmp(line, "route ", 6) == 0 &&
             frontend->set_local_route != NULL && frontend->read_local_route != NULL)
        result = execute_local_route(frontend, line, write, write_context);
    (void)write_record(write, write_context,
                       result == GRIDPICO_OK ? "OK" : error_record(result),
                       strlen(result == GRIDPICO_OK ? "OK" : error_record(result)));
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
    *frontend = (gridpico_cli_frontend){
        .session_context = session_context,
        .read_snapshot = read_snapshot,
        .read_device_color = read_device_color,
        .set_device_color = set_device_color,
        .log_context = log_context,
        .read_logs = read_logs,
        .terminal_write = terminal_write,
        .terminal_write_context = terminal_write_context,
    };
    EmbeddedCliConfig config = *embeddedCliDefaultConfig();
    config.invitation = "> ";
    config.rxBufferSize = GRIDPICO_CLI_RX_BYTES;
    /* embedded-cli reserves two bytes in the command buffer. */
    config.cmdBufferSize = GRIDPICO_CLI_LINE_BYTES + 2;
    config.historyBufferSize = GRIDPICO_CLI_HISTORY_BYTES;
    config.maxBindingCount = 1;
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
        embeddedCliProcess(frontend->terminal);
    }
}
