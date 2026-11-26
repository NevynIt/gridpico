#include "app/rp2040_pat2_projection.h"
#ifdef GRIDPICO_PAT2_APPLY_HOST
#include "app/rp2040_pat2_capture.h"
#include "app/rp2040_auto_routes.h"
#include "frontends/cli/cli_frontend.h"
#include <inttypes.h>
#endif

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CFG_TUH_MIDI 7
#define CFG_TUH_DEVICE_MAX 7
#define CFG_TUH_HUB 2
#define NO_INTERFACE UINT8_MAX

static gridpico_pico_router router;
static uint8_t host_endpoints_out[CFG_TUH_MIDI];
static bool mounted[CFG_TUH_MIDI], descriptor_present[CFG_TUH_DEVICE_MAX + 1];

typedef struct {uint16_t bcdDevice;} tusb_desc_device_t;
static bool tuh_descriptor_get_device_local(uint8_t address, tusb_desc_device_t *out)
{
    if (!descriptor_present[address]) return false;
    *out = (tusb_desc_device_t){.bcdDevice = 0x0112};
    return true;
}
static uint16_t tu_le16toh(uint16_t value) { return value; }
static bool tuh_midi_mounted(uint8_t index) { return mounted[index]; }

static bool fixture_path(uint8_t address)
{
    return address != 0 && address <= CFG_TUH_DEVICE_MAX;
}

/* Only the role's already verified transport-path predicate is mocked. */
#define observed_path(address, path) ((void)(path), fixture_path(address))
#include "firmware.inc"
#undef observed_path

static void observe(uint8_t index, char serial)
{
    const uint8_t address = index + 1;
    interfaces[index] = (host_interface){
        .present = true, .device_address = address, .vid = 0x09e8, .pid = 0x004e,
        .number = 1, .rx_cables = 2, .tx_cables = 2,
    };
    mounted[index] = descriptor_present[address] = devices_ready[address] = true;
    device_streams[address] = 1;
    host_endpoints_out[index] = 2;
    observations[index] = (midi_observation){
        .lifetime = ++next_midi_lifetime, .input_endpoint = 0x81, .input_cables = 2, .output_cables = 2,
        .supported = true, .verified = serial != 0, .name = "Equal name",
    };
    assert(read_identity(index, &observations[index].identity));
    observations[index].identity.serial[0] = serial;
}

static void admit_fixture(uint8_t index, size_t device, gridpico_pico_peer first)
{
    assert(gridpico_pico_router_register_device(&router, first, observations[index].name, 3, 3) == GRIDPICO_OK);
    discovered[device] = (discovered_device){
        .identity = observations[index].identity, .lifetime = observations[index].lifetime,
        .index = index, .first_peer = first,
    };
    gridpico_pico_router_attach(&router, first, true);
    gridpico_pico_router_attach(&router, (gridpico_pico_peer)(first + 1), true);
}

#ifndef GRIDPICO_PAT2_APPLY_HOST
static gridpico_pico_pat2_identity saved_identity(uint8_t serial)
{
    return (gridpico_pico_pat2_identity){
        .vid = 0x09e8, .pid = 0x004e, .bcd_device = 0x0112,
        .interface_number = 1, .input_endpoint = 0x81, .output_endpoint = 2,
        .input_cables = 3, .output_cables = 3, .serial_length = 1, .serial = {serial},
    };
}

static gridpico_pico_pat2_intent saved_intent(void)
{
    gridpico_pico_pat2_intent intent = {
        .generation = 5, .identity_count = 2, .direction_count = 8, .pair_count = 2,
        .identities = {saved_identity('A'), saved_identity('B')},
        .slots = {1, 3, 5, 7, 0, 2, 4, 6, 8},
        .pairs = {{1, 6, 0}, {5, 2, 1}},
    };
    for (uint8_t device = 0; device < 2; ++device) {
        for (uint8_t cable = 0; cable < 2; ++cable) {
            const uint8_t base = 4 * device + 2 * cable;
            for (uint8_t direction = 0; direction < 2; ++direction) {
                intent.directions[base + direction] = (gridpico_pico_pat2_direction){
                    .identity_ref = (uint8_t)(device + 1), .cable = cable,
                    .direction = direction == 0 ? GRIDPICO_PATCH_INPUT : GRIDPICO_PATCH_OUTPUT,
                    .label = "Equal name",
                };
            }
        }
    }
    return intent;
}

static void capture(const gridpico_pico_pat2_intent *intent, gridpico_pico_pat2_projection *projection,
                    gridpico_pico_pat2_plan *plan)
{
    static gridpico_pico_router before;
    apc_roles roles;
    memcpy(&before, &router, sizeof(before));
    memcpy(&roles, &physical_roles, sizeof(roles));
    assert(gridpico_pico_project_pat2_view(projection) == GRIDPICO_OK);
    assert(gridpico_pico_plan_saved_intent(intent, &projection->view, plan) == GRIDPICO_OK);
    assert(memcmp(&before, &router, sizeof(router)) == 0);
    assert(memcmp(&roles, &physical_roles, sizeof(roles)) == 0);
    assert(plan->graph_revision == router.graph.revision &&
           plan->layout_revision == router.session.apc_layout_revision &&
           projection->view.branch_limit == router.graph.branch_limit);
}

int main(void)
{
    const gridpico_pico_peer b_peer = GRIDPICO_PICO_DISCOVERED;
    const gridpico_pico_peer a_peer = (gridpico_pico_peer)(b_peer + 2);
    enum { B0 = 0, B1 = 1, A0 = 2, A1 = 3 };
    assert(gridpico_pico_router_init(&router) == GRIDPICO_OK);
    observe(0, 'B');
    observe(2, 'A');
    admit_fixture(0, 0, b_peer);
    admit_fixture(2, 1, a_peer);
    physical_roles.panel = router.discovered[A1].input;
    physical_roles.led = router.discovered[B1].output;
    physical_roles.panel_explicit = true;
    assert(gridpico_patch_graph_set_apc_panel_reserved(&router.graph, physical_roles.panel, true) == GRIDPICO_OK);
    assert(gridpico_patch_graph_set_apc_led_reserved(&router.graph, physical_roles.led, true) == GRIDPICO_OK);
    const gridpico_local_route_request existing = {
        {router.discovered[B0].input, router.discovered[A0].output, GRIDPICO_LOCAL_MANAGED}, true,
    };
    assert(gridpico_patch_graph_set_local_route(&router.graph, &existing) == GRIDPICO_OK);
    physical_roles.graph_revision = router.graph.revision;

    const gridpico_pico_pat2_intent intent = saved_intent();
    gridpico_pico_pat2_projection projection;
    gridpico_pico_pat2_plan plan;
    capture(&intent, &projection, &plan);
    assert(projection.view.observation_count == 2 && projection.view.binding_count == 8);
    assert(projection.observations[0].identity.serial[0] == 'B' &&
           projection.observations[1].identity.serial[0] == 'A');
    assert(projection.view.branch_count == 1 && projection.graph.routes[0].route.source == existing.route.source);
    assert(plan.directions[0].endpoint == router.discovered[A0].input &&
           plan.directions[4].endpoint == router.discovered[B0].input);
    assert(plan.pairs[0].decision == GRIDPICO_PICO_PAT2_PAIR_OFF &&
           plan.pairs[0].destination.endpoint == router.discovered[B0].output &&
           plan.pairs[1].decision == GRIDPICO_PICO_PAT2_PAIR_ON);
    /* Exact Panel IN and LED OUT are reserved. A's other cable-1 direction is
     * withheld while its role remains optional, with B's cable-1 IN independent.
     */
    assert(plan.directions[2].endpoint == 0 && plan.directions[3].endpoint == 0 &&
           plan.directions[6].endpoint == router.discovered[B1].input && plan.directions[7].endpoint == 0);
    physical_roles.led_explicit = true;
    capture(&intent, &projection, &plan);
    assert(plan.directions[3].endpoint == router.discovered[A1].output && plan.directions[7].endpoint == 0);

    observe(3, 'A'); /* Exact duplicate with no admitted binding. */
    capture(&intent, &projection, &plan);
    assert(projection.view.observation_count == 3 && projection.view.binding_count == 8);
    assert(plan.directions[0].endpoint == 0 && plan.directions[4].endpoint == router.discovered[B0].input);
    assert(plan.slots[0].direction_ref == 1 && plan.pairs[0].decision == GRIDPICO_PICO_PAT2_PAIR_UNRESOLVED);

    observe(3, 0); /* Incomplete same-shape competitor, including missing bcdDevice. */
    descriptor_present[4] = false;
    interfaces[3].rx_cables = 1; /* The untrusted mount count disagrees with the descriptor. */
    capture(&intent, &projection, &plan);
    assert(projection.observations[2].identity.serial_length == 0 && !projection.observations[2].revision_known);
    assert(projection.observations[2].identity.input_cables == 3 &&
           plan.directions[0].endpoint == 0 && plan.directions[4].endpoint == 0);

    observations[3].supported = false; /* No shape can be represented as a blocker. */
    assert(gridpico_pico_project_pat2_view(&projection) == GRIDPICO_UNAVAILABLE);
    assert(projection.unknown_shape_interfaces == (1u << 3) && projection.view.observation_count == 0);
    mounted[3] = false;

    capture(&intent, &projection, &plan);
    const gridpico_connection old_input = plan.directions[0], old_output = plan.directions[1];
    assert(old_input.generation == router.discovered[A0].input_generation);
    assert(gridpico_pico_router_renew_managed_input(&router, a_peer) == GRIDPICO_OK);
    assert(gridpico_pico_project_pat2_view(&projection) == GRIDPICO_UNAVAILABLE);
    physical_roles.graph_revision = router.graph.revision;
    capture(&intent, &projection, &plan);
    assert(plan.directions[0].endpoint == old_input.endpoint && plan.directions[0].generation > old_input.generation &&
           plan.directions[1].endpoint == old_output.endpoint && plan.directions[1].generation == old_output.generation);
    assert(plan.pairs[0].source.generation == router.discovered[A0].input_generation);
    discovery_dirty = true;
    assert(gridpico_pico_project_pat2_view(&projection) == GRIDPICO_UNAVAILABLE);
    puts("PASS actual Pico PAT2 projection and planner: same labels, whole-set blockers, roles, and fresh generation");
    return 0;
}

#else
#include "pat2_apply_host.inc"
#endif
