#ifndef GRIDPICO_APP_PATCH_RUNTIME_H
#define GRIDPICO_APP_PATCH_RUNTIME_H

#include "core/midi/patch_graph.h"
#include "core/session/patch_session.h"
#include "frontends/apc/apc_patch.h"
#include "frontends/cli/cli_frontend.h"
#include "sim/platform/patch_fixture.h"

typedef struct {
    gridpico_note_delivery notes[GRIDPICO_PATCH_MONITOR_RELEASE_CREDITS];
    size_t note_count;
    gridpico_apc_surface_message leds[GRIDPICO_PATCH_LED_FRAMES];
    size_t led_count;
    gridpico_patch_led_page page;
    bool has_page;
    bool has_monitor_state;
    bool overflowed;
} patch_output;

typedef struct {
    gridpico_patch_graph graph;
    gridpico_patch_session session;
    gridpico_patch_fixture sim;
    gridpico_apc_patch apc;
    gridpico_connection keybed;
    uint32_t monitor_generation;
    bool monitor_attached;
    patch_output output;
    gridpico_diagnostic_log_entry logs[GRIDPICO_DIAGNOSTIC_LOG_ROWS];
    size_t log_start;
    size_t log_count;
    uint64_t next_log_sequence;
} patch_runtime;

/* Use one serialized caller and clear output between dispatches. */
gridpico_result patch_runtime_init(patch_runtime *runtime);
gridpico_result patch_runtime_init_browser(patch_runtime *runtime);
gridpico_result patch_runtime_init_monitor(patch_runtime *runtime);
gridpico_result patch_runtime_bind_cli(patch_runtime *runtime, gridpico_cli_frontend *cli,
                                      gridpico_cli_write_fn write, void *write_context);
void patch_runtime_clear_output(patch_runtime *runtime);
gridpico_result patch_runtime_refresh(patch_runtime *runtime, gridpico_patch_snapshot *snapshot);
gridpico_result patch_runtime_set_route(void *context, const gridpico_patch_route_request *request);
gridpico_result patch_runtime_submit(patch_runtime *runtime, const gridpico_ingress_note *input);
gridpico_result patch_runtime_update_port(patch_runtime *runtime,
                                         const gridpico_patch_port_update *update);
gridpico_result patch_runtime_set_monitor(patch_runtime *runtime, uint32_t generation, bool attached,
                                          gridpico_patch_snapshot *snapshot, bool *has_inventory);
/* Retain staged releases and append any accepted teardown retries. */
gridpico_result patch_runtime_disconnect_keybed(patch_runtime *runtime);
gridpico_result patch_runtime_retire(patch_runtime *runtime);

#endif
