#ifndef GRIDPICO_APP_LOCAL_RUNTIME_H
#define GRIDPICO_APP_LOCAL_RUNTIME_H

#include "app/local_profile.h"
#include "core/midi/patch_graph.h"
#include "core/session/patch_session.h"
#include "frontends/apc/apc_patch.h"
#include "frontends/cli/cli_frontend.h"

enum {
    LOCAL_SERIAL_QUEUE = 8192,
    LOCAL_SERIAL_CHUNK = 256,
    LOCAL_RAW_LEASE_MS = 30000,
    /* One dispatch can retire all credits, render feedback and stage faults. */
    LOCAL_MIDI_STAGE_FRAMES = GRIDPICO_LOCAL_MAX_RELEASE_CREDITS +
        2 * GRIDPICO_PATCH_LED_FRAMES + GRIDPICO_LOCAL_MAX_FANOUT + 1,
};

typedef struct {
    void *context;
    gridpico_result (*send)(void *context, gridpico_connection destination,
                            const uint8_t bytes[GRIDPICO_LOCAL_MESSAGE_BYTES]);
    void (*retire)(void *context, gridpico_connection connection);
} local_midi_adapter;

typedef struct {
    gridpico_patch_direction direction;
    unsigned limit;
    uint32_t generation;
    bool attached;
    bool adapter_owned;
    bool socket;
} local_port_state;

typedef struct {
    uint32_t generation;
    size_t queued;
    bool attached;
    bool fault;
    bool fault_staged;
    bool adapter_owned;
    local_midi_adapter adapter;
} local_binding;

typedef struct {
    gridpico_connection destination;
    uint8_t bytes[GRIDPICO_LOCAL_MESSAGE_BYTES];
    bool fault;
} local_output;

typedef struct {
    gridpico_patch_graph graph;
    gridpico_patch_session session;
    gridpico_apc_patch apc;
    gridpico_cli_frontend cli;
    const local_router_profile *profile;
    gridpico_port_id feedback_port;
    gridpico_port_id sequence_port;
    bool configured;
    bool routing_enabled;
    local_binding bindings[GRIDPICO_PATCH_MAX_PORTS];
    local_binding serial;
    local_output output[LOCAL_MIDI_STAGE_FRAMES + GRIDPICO_PATCH_MAX_PORTS];
    size_t output_count;
    size_t fault_count;
    char serial_bytes[LOCAL_SERIAL_QUEUE];
    size_t serial_count;
    gridpico_diagnostic_log_entry logs[GRIDPICO_DIAGNOSTIC_LOG_ROWS];
    size_t log_start;
    size_t log_count;
    uint64_t log_sequence;
    uint64_t now_ms;
    uint64_t raw_deadline_ms;
    gridpico_local_route raw_route;
    bool feedback_ready;
    bool feedback_refresh;
    bool refreshing;
} local_runtime;

gridpico_result local_runtime_init(local_runtime *runtime);
/* A configured profile remains valid for the lifetime of the runtime. */
gridpico_result local_runtime_init_profile(local_runtime *runtime, const local_router_profile *profile);
gridpico_result local_runtime_enable_web_ui_routes(local_runtime *runtime);
gridpico_result local_runtime_enable_recorder_route(local_runtime *runtime);
void local_runtime_begin(local_runtime *runtime, uint64_t now_ms);
gridpico_result local_runtime_finish(local_runtime *runtime, gridpico_result result);
gridpico_result local_runtime_bind(local_runtime *runtime, gridpico_connection connection,
                                  gridpico_patch_direction *direction, unsigned *limit);
gridpico_result local_runtime_bind_managed_out(local_runtime *runtime,
                                              gridpico_connection connection,
                                              gridpico_patch_direction *direction, unsigned *limit);
gridpico_result local_runtime_port(const local_runtime *runtime, gridpico_port_id id,
                                  local_port_state *out);
gridpico_result local_runtime_reserve_adapter(local_runtime *runtime, gridpico_port_id id);
gridpico_result local_runtime_bind_adapter(local_runtime *runtime, gridpico_connection connection,
                                           local_midi_adapter adapter);
gridpico_result local_runtime_detach(local_runtime *runtime, gridpico_connection connection);
gridpico_result local_runtime_midi(local_runtime *runtime, const gridpico_local_raw_ingress *input);
gridpico_result local_runtime_serial_bind(local_runtime *runtime, uint32_t generation);
gridpico_result local_runtime_serial_detach(local_runtime *runtime, uint32_t generation);
gridpico_result local_runtime_serial(local_runtime *runtime, uint32_t generation,
                                    const char *bytes, size_t size);
gridpico_result local_runtime_expire(local_runtime *runtime);
gridpico_result local_runtime_retire(local_runtime *runtime);

#endif
