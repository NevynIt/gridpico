/* All operations below belong to the serialized router foreground. */
static bool pico_panic_cdc_current(void)
{
    return panic_cdc_generation != 0 && panic_cdc_generation != UINT32_MAX &&
        native_bus_epoch != 0 && native_bus_epoch != UINT32_MAX &&
        native_bus_epoch == native_accepted_reset_epoch && native_bus_epoch == native_mounted_epoch && tud_mounted() &&
        command.dtr && tud_cdc_connected() && !router.keys_mode && !router.keys_cutover;
}

static bool same_panic_panel(const pico_panic_panel_owner *a, const pico_panic_panel_owner *b)
{
    return a->port == b->port && a->frontend_generation == b->frontend_generation &&
        a->input_generation == b->input_generation && a->lifetime == b->lifetime &&
        a->index == b->index && a->peer == b->peer && a->cable == b->cable &&
        a->address == b->address && a->interface_number == b->interface_number && a->endpoint == b->endpoint;
}

static void check_pico_panic_owner(void)
{
    if (pico_panic.report.state != GRIDPICO_PANIC_STAGE_RUNNING) return;
    if (pico_panic_owner.kind == PICO_PANIC_OWNER_CDC &&
        pico_panic_cdc_current() && pico_panic.caller == panic_cdc_generation &&
        pico_panic.bus_epoch == native_bus_epoch) return;
    if (pico_panic_owner.kind == PICO_PANIC_OWNER_PANEL && physical_panic_mode()) {
        pico_panic_panel_owner current;
        if (read_panic_panel_owner(&current) && same_panic_panel(&pico_panic_owner.panel, &current)) return;
    }
    gridpico_pico_panic_cancel(&pico_panic);
}

static bool resolve_pico_panic(gridpico_port_id id, gridpico_pico_panic_path *out)
{
    const gridpico_patch_graph *graph = &router.graph;
    const gridpico_patch_inventory *inv = &graph->inventory;
    for (size_t i = 0; i < inv->port_count; ++i) {
        const gridpico_patch_port *port = &inv->ports[i];
        if (port->id != id) continue;
        if (port->direction != GRIDPICO_PATCH_OUTPUT || port->state != GRIDPICO_PATCH_CONNECTED ||
            !port->patch_eligible || graph->port_capabilities[i] != GRIDPICO_LOCAL_PORT_MANAGED ||
            graph->ports[i].apc_led_reserved || graph->ports[i].apc_panel_reserved ||
            graph->ports[i].generation == 0) return false;
        const gridpico_connection output = {id, graph->ports[i].generation};
        if (id == graph->native_virtual_cable.destination) {
            if (!native_epoch_current() || !router.native_output_ready || !router.attached[GRIDPICO_PICO_WEB] ||
                output.generation != router.generation[GRIDPICO_PICO_WEB]) return false;
            *out = (gridpico_pico_panic_path){
                .output = output, .lifetime = native_active_epoch, .configuration = native_active_configuration,
                .index = GRIDPICO_PICO_PANIC_NATIVE, .endpoint = GRIDPICO_NATIVE_MIDI_IN_EP, .peer = GRIDPICO_PICO_WEB,
            };
            return true;
        }
        usb_midi_lane lane;
        if (lookup_lane(id, true, &lane) != GRIDPICO_OK || !observations[lane.index].verified ||
            output.generation != router.generation[lane.peer]) return false;
        const host_interface *itf = &interfaces[lane.index];
        const uint8_t endpoint = host_endpoints_out[lane.index];
        if (lane.cable >= itf->tx_cables || endpoint == 0 || tu_edpt_dir(endpoint) != TUSB_DIR_OUT) return false;
        *out = (gridpico_pico_panic_path){
            .output = output, .lifetime = observations[lane.index].lifetime, .index = lane.index,
            .address = itf->device_address, .interface_number = itf->number, .endpoint = endpoint,
            .cable = lane.cable, .peer = (uint8_t)lane.peer,
        };
        return true;
    }
    return false;
}

static gridpico_result read_pico_panic(void *context, gridpico_panic_stage_report *out)
{
    if (!pico_panic_cdc_current()) return GRIDPICO_UNAVAILABLE;
    const gridpico_pico_panic *panic = context;
    *out = panic->report;
    return GRIDPICO_OK;
}

static gridpico_result start_pico_panic(void *context, gridpico_panic_stage_report *out)
{
    check_pico_panic_owner();
    if (!pico_panic_cdc_current()) return GRIDPICO_UNAVAILABLE;
    const gridpico_result result = gridpico_pico_panic_start(context, panic_cdc_generation, native_bus_epoch,
                                                           &router.graph.inventory, resolve_pico_panic, out);
    if (result == GRIDPICO_OK) pico_panic_owner.kind = PICO_PANIC_OWNER_CDC;
    return result;
}

static gridpico_result start_physical_panic(void *panel_context)
{
    if (panel_context != &router.panels[GRIDPICO_PICO_APC] || !physical_panic_admitted()) return GRIDPICO_UNAVAILABLE;
    check_pico_panic_owner();
    pico_panic_panel_owner panel;
    if (!read_panic_panel_owner(&panel)) return GRIDPICO_UNAVAILABLE;
    const gridpico_result result = gridpico_pico_panic_start(
        &pico_panic, panel.frontend_generation, panel.lifetime, &router.graph.inventory, resolve_pico_panic, &panic_reply);
    if (result == GRIDPICO_OK) {
        pico_panic_owner.panel = panel;
        pico_panic_owner.kind = PICO_PANIC_OWNER_PANEL;
    }
    return result;
}

static void finish_pico_panic(gridpico_pico_panic_ticket ticket, bool exact)
{
    check_pico_panic_owner();
    gridpico_pico_panic_complete(&pico_panic, ticket, exact);
}

static bool native_panic_ready(void)
{
    const uint32_t irq = save_and_disable_interrupts();
    const bool ready = native_epoch_current() && native_out.kind == NATIVE_OUT_IDLE && !tud_suspended() &&
        (native_out_started || !tud_task_event_ready()) && native_endpoint_quiet() &&
        usbd_edpt_ready(DEVICE_ROOT, GRIDPICO_NATIVE_MIDI_IN_EP);
    restore_interrupts(irq);
    return ready;
}

static bool host_panic_ready(const gridpico_pico_panic_path *path)
{
    const host_interface *itf = &interfaces[path->index];
    if (itf->busy) return false;
    if (itf->out_lifetime == path->lifetime) return true;
    const uint32_t irq = save_and_disable_interrupts();
    const bool ready = !tuh_task_event_ready();
    restore_interrupts(irq);
    return ready;
}

static gridpico_pico_panic_submit submit_pico_panic(const gridpico_pico_panic_path *path,
    const gridpico_pico_packet *packet, gridpico_pico_panic_ticket ticket)
{
    if (path->index == GRIDPICO_PICO_PANIC_NATIVE) {
        if (!native_panic_ready()) return GRIDPICO_PICO_PANIC_DEFER;
        if (!submit_native_packet(packet, NATIVE_OUT_PANIC))
            return native_panic_ready() ? GRIDPICO_PICO_PANIC_REFUSED : GRIDPICO_PICO_PANIC_DEFER;
        native_out.panic_ticket = ticket;
        add_trace('G', GRIDPICO_PICO_WEB, packet->bytes, sizeof packet->bytes);
    } else {
        if (!host_panic_ready(path)) return GRIDPICO_PICO_PANIC_DEFER;
        if (!submit_host_packet(path->index, (gridpico_pico_peer)path->peer, packet, path->cable))
            return host_panic_ready(path) ? GRIDPICO_PICO_PANIC_REFUSED : GRIDPICO_PICO_PANIC_DEFER;
        interfaces[path->index].panic_ticket = ticket;
        add_trace('G', path->peer, packet->bytes, sizeof packet->bytes);
    }
    return GRIDPICO_PICO_PANIC_ISSUED;
}

static void service_pico_panic(void)
{
    if (pico_panic.report.state != GRIDPICO_PANIC_STAGE_RUNNING) return;
    check_pico_panic_owner();
    gridpico_pico_panic_service(&pico_panic, resolve_pico_panic, submit_pico_panic);
}
