#include "app/rp2040_panic.h"

#include <string.h>

enum { CHANNELS = 16, ATTEMPTS_PER_PASS = 8 };

static bool same_path(const gridpico_pico_panic_path *a, const gridpico_pico_panic_path *b)
{
    return a->output.endpoint == b->output.endpoint && a->output.generation == b->output.generation &&
        a->lifetime == b->lifetime && a->configuration == b->configuration && a->index == b->index &&
        a->address == b->address && a->interface_number == b->interface_number &&
        a->endpoint == b->endpoint && a->cable == b->cable && a->peer == b->peer;
}

gridpico_result gridpico_pico_panic_start(gridpico_pico_panic *panic,
    uint32_t caller, uint32_t bus_epoch, const gridpico_patch_inventory *inventory,
    gridpico_pico_panic_resolve_fn resolve, gridpico_panic_stage_report *out)
{
    if (caller == 0 || bus_epoch == 0 || panic->report.state == GRIDPICO_PANIC_STAGE_RUNNING)
        return GRIDPICO_UNAVAILABLE;
    if (panic->job == UINT32_MAX) return GRIDPICO_CAPACITY;

    size_t count = 0;
    for (size_t i = 0; i < inventory->port_count; ++i) {
        const gridpico_patch_port *port = &inventory->ports[i];
        gridpico_pico_panic_path path;
        if (port->direction != GRIDPICO_PATCH_OUTPUT || !resolve(port->id, &path)) continue;
        if (count == GRIDPICO_PICO_PANIC_OUTPUTS) return GRIDPICO_CAPACITY;
        size_t slot = count++;
        while (slot != 0 && panic->snapshot[slot - 1].output.endpoint > path.output.endpoint) {
            panic->snapshot[slot] = panic->snapshot[slot - 1];
            --slot;
        }
        panic->snapshot[slot] = path;
    }

    memset(&panic->report, 0, sizeof panic->report);
    memset(panic->issued, 0, sizeof panic->issued);
    memcpy(panic->paths, panic->snapshot, count * sizeof panic->paths[0]);
    for (size_t i = 0; i < count; ++i) {
        panic->report.outputs[i] = (gridpico_panic_output){
            .output = panic->paths[i].output, .requested_count = CHANNELS,
            .state = GRIDPICO_PANIC_OUTPUT_PENDING,
        };
    }
    ++panic->job;
    panic->caller = caller;
    panic->bus_epoch = bus_epoch;
    panic->cursor = 0;
    panic->pending = (uint8_t)count;
    panic->report.count = count;
    panic->report.state = count != 0 ? GRIDPICO_PANIC_STAGE_RUNNING : GRIDPICO_PANIC_STAGE_COMPLETE;
    *out = panic->report;
    return GRIDPICO_OK;
}

void gridpico_pico_panic_cancel(gridpico_pico_panic *panic)
{
    if (panic->report.state != GRIDPICO_PANIC_STAGE_RUNNING) return;
    for (size_t i = 0; i < panic->report.count; ++i) {
        if (panic->report.outputs[i].state == GRIDPICO_PANIC_OUTPUT_PENDING)
            panic->report.outputs[i].state = GRIDPICO_PANIC_OUTPUT_CANCELLED;
        panic->issued[i] = false;
    }
    panic->pending = 0;
    panic->report.state = GRIDPICO_PANIC_STAGE_CANCELLED;
}

static void finish_row(gridpico_pico_panic *panic, size_t row, gridpico_panic_output_state state)
{
    gridpico_panic_output *output = &panic->report.outputs[row];
    if (state == GRIDPICO_PANIC_OUTPUT_UNCONFIRMED) output->accepted_count = 0;
    output->state = state;
    panic->issued[row] = false;
    if (--panic->pending == 0) panic->report.state = GRIDPICO_PANIC_STAGE_COMPLETE;
}

void gridpico_pico_panic_complete(gridpico_pico_panic *panic,
    gridpico_pico_panic_ticket ticket, bool exact)
{
    const size_t index = ticket.row;
    if (panic->report.state != GRIDPICO_PANIC_STAGE_RUNNING || ticket.job != panic->job ||
        index >= panic->report.count || !panic->issued[index]) return;
    gridpico_panic_output *output = &panic->report.outputs[index];
    if (ticket.channel != output->accepted_count) return;
    panic->issued[index] = false;
    if (!exact) finish_row(panic, index, GRIDPICO_PANIC_OUTPUT_UNCONFIRMED);
    else if (++output->accepted_count == output->requested_count)
        finish_row(panic, index, GRIDPICO_PANIC_OUTPUT_COMPLETE);
}

void gridpico_pico_panic_service(gridpico_pico_panic *panic,
    gridpico_pico_panic_resolve_fn resolve, gridpico_pico_panic_send_fn send)
{
    if (panic->report.state != GRIDPICO_PANIC_STAGE_RUNNING) return;
    unsigned attempts = 0;
    for (size_t polled = 0; polled < panic->report.count && attempts < ATTEMPTS_PER_PASS; ++polled) {
        const size_t index = panic->cursor;
        if (++panic->cursor == panic->report.count) panic->cursor = 0;
        const gridpico_panic_output *output = &panic->report.outputs[index];
        if (output->state != GRIDPICO_PANIC_OUTPUT_PENDING) continue;
        const gridpico_pico_panic_path *path = &panic->paths[index];
        gridpico_pico_panic_path current;
        if (!resolve(output->output.endpoint, &current) || !same_path(path, &current)) {
            finish_row(panic, index, panic->issued[index] ?
                GRIDPICO_PANIC_OUTPUT_UNCONFIRMED : GRIDPICO_PANIC_OUTPUT_CANCELLED);
            continue;
        }
        if (panic->issued[index]) continue;

        const gridpico_pico_panic_ticket ticket = {
            .job = panic->job, .row = (uint8_t)index, .channel = (uint8_t)output->accepted_count,
        };
        const gridpico_pico_packet packet = {{
            (uint8_t)((path->cable << 4) | 0x0bu), (uint8_t)(0xb0u + ticket.channel), 123, 0,
        }};
        const gridpico_pico_panic_submit result = send(path, &packet, ticket);
        if (result == GRIDPICO_PICO_PANIC_DEFER) continue;
        ++attempts;
        if (result == GRIDPICO_PICO_PANIC_ISSUED) panic->issued[index] = true;
        else finish_row(panic, index, GRIDPICO_PANIC_OUTPUT_REFUSED);
    }
}
