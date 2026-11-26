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
    LOCAL_FORWARDING_BARRIERS = 16,
    LOCAL_RAW_LEASE_MS = 30000,
    /* One dispatch can render feedback as well as its MIDI fan-out. */
    LOCAL_MIDI_STAGE_FRAMES = 2 * GRIDPICO_PATCH_LED_FRAMES + GRIDPICO_LOCAL_MAX_FANOUT + 1,
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
    bool managed;
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
    gridpico_revision forwarding_revision;
    uint8_t bytes[GRIDPICO_LOCAL_MESSAGE_BYTES];
    bool fault;
    bool panic;
} local_output;

typedef struct {
    gridpico_revision revision;
    size_t serial_offset;
    bool running;
} local_forwarding_barrier;

typedef struct {
    uint64_t runtime_epoch;
    gridpico_device_id device;
    gridpico_connection output;
} local_fm_lease;

typedef enum {
    LOCAL_FM_FREE,
    LOCAL_FM_PENDING,
    LOCAL_FM_ATTACHED,
    LOCAL_FM_FENCED,
} local_fm_state;

typedef struct {
    local_fm_lease lease;
    const void *reservation_owner;
    const void *socket_owner;
    uint64_t deadline_ms;
    char name[GRIDPICO_PATCH_NAME_BYTES];
    local_fm_state state;
} local_fm_allocation;

typedef struct {
    gridpico_patch_graph graph;
    gridpico_patch_session session;
    gridpico_apc_patch apc;
    gridpico_cli_frontend cli;
    gridpico_local_routing_snapshot routing_scratch;
    const local_router_profile *profile;
    gridpico_port_id feedback_port;
    gridpico_port_id sequence_port;
    bool configured;
    bool routing_enabled;
    bool note_transform_enabled;
    bool recording_enabled;
    bool forwarding_transport;
    bool forwarding_enabled;
    bool forwarding_running;
    gridpico_revision forwarding_revision;
    local_forwarding_barrier forwarding_barriers[LOCAL_FORWARDING_BARRIERS];
    size_t forwarding_barrier_count;
    local_binding bindings[GRIDPICO_PATCH_MAX_PORTS];
    uint64_t fm_runtime_epoch;
    uint32_t next_fm_device;
    uint32_t next_fm_port;
    local_fm_allocation fm_allocations[GRIDPICO_PATCH_MAX_DEVICES];
    local_binding serial;
    gridpico_panic_stage_report panic_report;
    gridpico_panic_stage_report panic_scratch;
    uint32_t panic_owner;
    size_t panic_cursor;
    size_t panic_pending;
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
/* Private entry for a qualified portable join owner. The caller supplies the
 * runtime lifetime epoch and serializes all operations with ordinary dispatch.
 * Owner tokens identify already verified claims and are never dereferenced. */
gridpico_result local_runtime_init_fm_profile(local_runtime *runtime, const local_router_profile *profile,
                                             uint64_t runtime_epoch);
/* Prepare Graph backing at startup. An eligible runner arms the epoch once
 * after its private negotiation, without restarting Graph or Session. */
gridpico_result local_runtime_prepare_fm_profile(local_runtime *runtime, const local_router_profile *profile);
uint64_t local_runtime_fm_deadline(const local_runtime *runtime);
local_fm_state local_runtime_fm_lease_state(local_runtime *runtime, local_fm_lease lease,
                                          const void *reservation_owner);
gridpico_result local_runtime_reserve_fm(local_runtime *runtime, const char *name, uint64_t deadline_ms,
                                       const void *reservation_owner, local_fm_lease *out);
gridpico_result local_runtime_publish_fm(local_runtime *runtime, local_fm_lease lease,
                                       const void *reservation_owner, const void *socket_owner);
/* A pending reservation is cancelled with a null socket owner. A published
 * lease must supply its exact socket owner after fencing the host side. */
gridpico_result local_runtime_retire_fm(local_runtime *runtime, local_fm_lease lease,
                                      const void *reservation_owner, const void *socket_owner);
gridpico_result local_runtime_enable_web_ui_routes(local_runtime *runtime);
gridpico_result local_runtime_enable_recorder_route(local_runtime *runtime);
/* Desktop assembly checks the validated profile before serial ingress. */
void local_runtime_enable_desktop_note_transform(local_runtime *runtime);
gridpico_result local_runtime_bind_forwarding_transport(local_runtime *runtime);
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
/* Zero is the legacy, untagged pipe form. */
gridpico_result local_runtime_midi_at_revision(local_runtime *runtime,
                                               const gridpico_local_raw_ingress *input,
                                               gridpico_revision revision);
gridpico_result local_runtime_serial_bind(local_runtime *runtime, uint32_t generation);
gridpico_result local_runtime_serial_detach(local_runtime *runtime, uint32_t generation);
gridpico_result local_runtime_serial(local_runtime *runtime, uint32_t generation,
                                    const char *bytes, size_t size);
void local_runtime_service_panic(local_runtime *runtime);
gridpico_result local_runtime_expire(local_runtime *runtime);
gridpico_result local_runtime_retire(local_runtime *runtime);

#endif
