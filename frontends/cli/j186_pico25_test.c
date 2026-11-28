#include "frontends/cli/cli_frontend.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    gridpico_patch_routing_metadata metadata;
    gridpico_patch_routing_page scratch;
    size_t page_reads;
    size_t writes;
    bool bad_count;
    bool fail_page;
    bool corrupt_page;
    bool first_route_enabled;
} provider;

typedef struct { char bytes[8192]; size_t size; } output;

static void capture(void *context, const char *bytes, size_t size)
{
    output *out = context;
    assert(size <= sizeof out->bytes - out->size - 1);
    memcpy(out->bytes + out->size, bytes, size);
    out->size += size;
    out->bytes[out->size] = '\0';
}

static gridpico_result no_snapshot(void *context, gridpico_patch_snapshot *out)
{
    (void)context; (void)out; return GRIDPICO_UNAVAILABLE;
}

static void init_provider(provider *p)
{
    memset(p, 0, sizeof *p);
    p->metadata.revision = 77;
    p->metadata.graph.revision = 9;
    p->metadata.graph.registry.inventory.device_count = 2;
    p->metadata.graph.registry.inventory.port_count = 12;
    p->metadata.graph.registry.inventory.devices[0] = (gridpico_patch_device){
        .id = 1, .state = GRIDPICO_PATCH_DISCONNECTED,
    };
    p->metadata.graph.registry.inventory.devices[1] = (gridpico_patch_device){
        .id = 2, .state = GRIDPICO_PATCH_DISCONNECTED,
    };
    strcpy(p->metadata.graph.registry.inventory.devices[0].name, "Input device");
    strcpy(p->metadata.graph.registry.inventory.devices[1].name, "Output device");
    for (size_t i = 0; i < 6; ++i) {
        const bool raw = i == 5;
        p->metadata.graph.registry.inventory.ports[i] = (gridpico_patch_port){
            .id = (gridpico_port_id)(i + 1), .device = 1,
            .direction = GRIDPICO_PATCH_INPUT,
            .state = GRIDPICO_PATCH_DISCONNECTED,
            .patch_eligible = !raw,
        };
        p->metadata.graph.registry.inventory.ports[i + 6] = (gridpico_patch_port){
            .id = (gridpico_port_id)(i + 7), .device = 2,
            .direction = GRIDPICO_PATCH_OUTPUT,
            .state = GRIDPICO_PATCH_DISCONNECTED,
            .patch_eligible = !raw,
        };
        strcpy(p->metadata.graph.registry.inventory.ports[i].name,
               raw ? "Raw input" : "Managed input");
        strcpy(p->metadata.graph.registry.inventory.ports[i + 6].name,
               raw ? "Raw output" : "Managed output");
        p->metadata.graph.registry.port_capabilities[i] = raw
            ? GRIDPICO_LOCAL_PORT_RAW : GRIDPICO_LOCAL_PORT_MANAGED;
        p->metadata.graph.registry.port_capabilities[i + 6] = raw
            ? GRIDPICO_LOCAL_PORT_RAW : GRIDPICO_LOCAL_PORT_MANAGED;
        p->metadata.availability[i] = GRIDPICO_LOCAL_FREE;
        p->metadata.availability[i + 6] = GRIDPICO_LOCAL_FREE;
    }
    p->metadata.graph.enabled_count = 25;
    p->first_route_enabled = true;
}

static gridpico_result read_metadata(void *context,
                                     gridpico_patch_routing_metadata *out)
{
    provider *p = context;
    if (out == NULL) return GRIDPICO_INVALID;
    *out = p->metadata;
    if (p->bad_count) out->graph.enabled_count = 27;
    return GRIDPICO_OK;
}

static gridpico_local_enabled_route route_at(size_t n, bool first_enabled)
{
    gridpico_local_enabled_route result = {0};
    result.state = GRIDPICO_PATCH_ROUTE_ACTIVE;
    if (n < 25) {
        result.route.source = (gridpico_port_id)(1 + n / 5);
        result.route.destination = (gridpico_port_id)(7 + n % 5);
        result.route.mode = GRIDPICO_LOCAL_MANAGED;
        if (n == 0 && !first_enabled) result.state = GRIDPICO_PATCH_ROUTE_OFF;
    } else {
        assert(n == 25);
        result.route = (gridpico_local_route){6, 12, GRIDPICO_LOCAL_RAW};
    }
    return result;
}

static gridpico_result read_page(void *context, gridpico_revision expected_revision,
                                 size_t offset, gridpico_patch_routing_page *out)
{
    provider *p = context;
    ++p->page_reads;
    if (out == NULL) return GRIDPICO_INVALID;
    if (expected_revision != 0 && expected_revision != p->metadata.revision)
        return GRIDPICO_UNAVAILABLE;
    if (p->fail_page && offset == 14) return GRIDPICO_UNAVAILABLE;
    const size_t roster = p->metadata.graph.registry.inventory.device_count +
                          p->metadata.graph.registry.inventory.port_count;
    const size_t total = roster + p->metadata.graph.enabled_count;
    if (offset > total) return GRIDPICO_INVALID;
    memset(out, 0, sizeof *out);
    out->metadata = p->metadata;
    if (p->bad_count) out->metadata.graph.enabled_count = 27;
    out->record_count = total - offset < GRIDPICO_LOCAL_ROUTING_PAGE_RECORDS
        ? total - offset : GRIDPICO_LOCAL_ROUTING_PAGE_RECORDS;
    out->next_offset = offset + out->record_count;
    const size_t first_route_record = offset > roster ? offset : roster;
    if (out->next_offset > first_route_record) {
        out->route_count = out->next_offset - first_route_record;
        for (size_t i = 0; i < out->route_count; ++i) {
            const size_t route_index = first_route_record + i - roster;
            out->routes[i] = route_at(route_index, p->first_route_enabled);
        }
        if (p->corrupt_page && offset == 14 && out->route_count > 1)
            out->routes[1] = out->routes[0];
    }
    return GRIDPICO_OK;
}

static bool route_eligible(const provider *p, const gridpico_local_route *route)
{
    if (route->source == 6 && route->destination == 12 &&
        route->mode == GRIDPICO_LOCAL_RAW) return true;
    if (route->source < 1 || route->source > 5 ||
        route->destination < 7 || route->destination > 11 ||
        route->mode != GRIDPICO_LOCAL_MANAGED) return false;
    (void)p;
    return true;
}

static gridpico_result read_guarded_pair(void *context, gridpico_revision expected,
                                         const gridpico_local_route *route,
                                         gridpico_local_route_status *out)
{
    provider *p = context;
    if (expected != p->metadata.revision) return GRIDPICO_UNAVAILABLE;
    if (route == NULL || out == NULL || !route_eligible(p, route))
        return GRIDPICO_INVALID;
    *out = (gridpico_local_route_status){
        .revision = p->metadata.graph.revision,
        .state = route->source == 1 && route->destination == 7 &&
                 !p->first_route_enabled ? GRIDPICO_PATCH_ROUTE_OFF
                                         : GRIDPICO_PATCH_ROUTE_ACTIVE,
    };
    return GRIDPICO_OK;
}

static gridpico_result read_route(void *context, const gridpico_local_route *route,
                                  gridpico_local_route_status *out)
{
    return read_guarded_pair(context, ((provider *)context)->metadata.revision,
                             route, out);
}

static gridpico_result set_route(void *context,
                                 const gridpico_local_route_request *request)
{
    provider *p = context;
    if (request == NULL || !route_eligible(p, &request->route))
        return GRIDPICO_INVALID;
    if (request->route.source == 1 && request->route.destination == 7 &&
        request->route.mode == GRIDPICO_LOCAL_MANAGED) {
        if (p->first_route_enabled != request->enabled) {
            p->first_route_enabled = request->enabled;
            --p->metadata.graph.enabled_count;
            ++p->metadata.revision;
            ++p->metadata.graph.revision;
        }
    }
    ++p->writes;
    return GRIDPICO_OK;
}

static void assert_bounds(const output *out)
{
    size_t records = 0;
    size_t row = 0;
    for (size_t i = 0; i < out->size; ++i) {
        if (out->bytes[i] == '\n') {
            assert(i > 0 && out->bytes[i - 1] == '\r');
            assert(i - row + 1 <= 256);
            ++records;
            row = i + 1;
        }
    }
    assert(row == out->size);
    assert(records <= 10);
}

int main(void)
{
    provider p;
    init_provider(&p);
    gridpico_cli_frontend cli = {0};
    gridpico_patch_routing_page scratch = {0};
    output out = {0};
    assert(gridpico_cli_init(&cli, &p, no_snapshot, NULL, NULL,
                             NULL, NULL, capture, &out) == GRIDPICO_OK);
    gridpico_result format_result = gridpico_cli_execute_line(&cli, "format machine", capture, &out);
    if (format_result != GRIDPICO_OK) fprintf(stderr, "format result %d output [%s]\n", (int)format_result, out.bytes);
    assert(format_result == GRIDPICO_OK);
    assert(strcmp(out.bytes, "FORMAT MACHINE\r\nOK\r\n") == 0);
    out = (output){0};
    gridpico_cli_bind_routing_pages(&cli, &p, read_metadata, read_page,
                                    read_guarded_pair, read_route, set_route, &scratch);

    size_t routes_seen = 0;
    for (size_t offset = 0; offset < 39; offset += GRIDPICO_LOCAL_ROUTING_PAGE_RECORDS) {
        char line[GRIDPICO_CLI_LINE_BYTES];
        if (offset == 0) strcpy(line, "routing");
        else snprintf(line, sizeof line, "routing 77 %zu", offset);
        out = (output){0};
        assert(gridpico_cli_execute_line(&cli, line, capture, &out) == GRIDPICO_OK);
        assert(strstr(out.bytes, "\r\nOK\r\n") != NULL);
        assert_bounds(&out);
        const size_t roster = 14;
        const size_t end = offset + GRIDPICO_LOCAL_ROUTING_PAGE_RECORDS < 39
            ? offset + GRIDPICO_LOCAL_ROUTING_PAGE_RECORDS : 39;
        const size_t first_route = offset > roster ? offset : roster;
        for (size_t record = first_route; record < end; ++record) {
            const size_t route_index = record - roster;
            char expected[64];
            const int length = snprintf(expected, sizeof expected,
                "ROUTE %zu %zu MANAGED ACTIVE\r\n",
                1 + route_index / 5, 7 + route_index % 5);
            assert(length > 0 && (size_t)length < sizeof expected);
            assert(strstr(out.bytes, expected) != NULL);
            ++routes_seen;
        }
    }
    assert(routes_seen == 25);
    out = (output){0};
    assert(gridpico_cli_execute_line(&cli, "routing 77 39", capture, &out) == GRIDPICO_OK);
    assert(strcmp(out.bytes, "ROUTING 77 -\r\nOK\r\n") == 0);

    /* Raw IN/OUT adds the 26th eligible geometry cell. */
    p.metadata.graph.enabled_count = 26;
    out = (output){0};
    assert(gridpico_cli_execute_line(&cli, "routing", capture, &out) == GRIDPICO_OK);
    assert(strncmp(out.bytes, "ROUTING 77 8\r\n", 14) == 0);
    out = (output){0};
    assert(gridpico_cli_execute_line(&cli, "route 6 12 raw", capture, &out) == GRIDPICO_OK);
    assert(strcmp(out.bytes, "ROUTE 6 12 RAW ACTIVE\r\nOK\r\n") == 0);
    p.metadata.graph.registry.port_capabilities[11] = GRIDPICO_LOCAL_PORT_PANEL;
    out = (output){0};
    assert(gridpico_cli_execute_line(&cli, "route 6 12 raw off if 77",
                                     capture, &out) == GRIDPICO_INVALID);
    assert(strcmp(out.bytes, "ERR INVALID\r\n") == 0 && p.writes == 0);
    p.metadata.graph.registry.port_capabilities[11] = GRIDPICO_LOCAL_PORT_RAW;
    p.metadata.graph.enabled_count = 25;

    /* No prefix and no edit for a count beyond validated pair geometry. */
    p.bad_count = true;
    out = (output){0};
    assert(gridpico_cli_execute_line(&cli, "routing", capture, &out) == GRIDPICO_INVALID);
    assert(strcmp(out.bytes, "ERR INVALID\r\n") == 0);
    size_t writes_before = p.writes;
    out = (output){0};
    assert(gridpico_cli_execute_line(&cli, "route 1 7 managed off if 77",
                                     capture, &out) == GRIDPICO_INVALID);
    assert(strcmp(out.bytes, "ERR INVALID\r\n") == 0 && p.writes == writes_before);
    p.bad_count = false;

    /* A malformed or failed continuation is a standalone error, never a page prefix. */
    p.corrupt_page = true;
    out = (output){0};
    assert(gridpico_cli_execute_line(&cli, "routing 77 14", capture, &out) == GRIDPICO_INVALID);
    assert(strcmp(out.bytes, "ERR INVALID\r\n") == 0);
    p.corrupt_page = false;
    p.fail_page = true;
    out = (output){0};
    assert(gridpico_cli_execute_line(&cli, "routing 77 14", capture, &out) == GRIDPICO_UNAVAILABLE);
    assert(strcmp(out.bytes, "ERR UNAVAILABLE\r\n") == 0);
    p.fail_page = false;

    out = (output){0};
    assert(gridpico_cli_execute_line(&cli, "route 1 7 managed", capture, &out) == GRIDPICO_OK);
    assert(strcmp(out.bytes, "ROUTE 1 7 MANAGED ACTIVE\r\nOK\r\n") == 0);
    writes_before = p.writes;
    out = (output){0};
    assert(gridpico_cli_execute_line(&cli, "route 1 7 managed off if 76",
                                     capture, &out) == GRIDPICO_UNAVAILABLE);
    assert(strcmp(out.bytes, "ERR UNAVAILABLE\r\n") == 0 && p.writes == writes_before);
    out = (output){0};
    assert(gridpico_cli_execute_line(&cli, "route 1 7 managed off if 77",
                                     capture, &out) == GRIDPICO_OK);
    assert(strcmp(out.bytes, "ROUTE 1 7 MANAGED OFF\r\nOK\r\n") == 0);
    assert(p.writes == writes_before + 1);
    return 0;
}
