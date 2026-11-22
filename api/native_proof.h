#ifndef GRIDPICO_NATIVE_PROOF_H
#define GRIDPICO_NATIVE_PROOF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* In-process value types for the Linux route and transpose proof. */
typedef uint16_t gridpico_endpoint_id;
typedef uint64_t gridpico_revision;

typedef struct {
    gridpico_endpoint_id endpoint;
    uint32_t generation;
} gridpico_connection;

typedef enum {
    GRIDPICO_NOTE_ON,
    GRIDPICO_NOTE_OFF
} gridpico_note_kind;

typedef struct {
    gridpico_note_kind kind;
    uint8_t channel_index; /* 0–15 internally, displayed as 1–16. */
    uint8_t key;
    uint8_t velocity;
} gridpico_note;

typedef struct {
    gridpico_connection source;
    gridpico_note note;
} gridpico_ingress_note;

typedef struct {
    gridpico_connection source;
    gridpico_connection destination;
    gridpico_note note;
} gridpico_note_delivery;

typedef enum {
    GRIDPICO_OK,
    GRIDPICO_INVALID,
    GRIDPICO_CAPACITY,
    GRIDPICO_UNAVAILABLE
} gridpico_result;

/* The caller owns the ordered offsets for the duration of Apply. */
typedef struct {
    gridpico_endpoint_id source;
    gridpico_endpoint_id destination;
    const int8_t *transpose_offsets;
    size_t transpose_count;
} gridpico_route_request;

typedef struct {
    gridpico_revision applied_revision;
    bool configured;
    bool active;
    gridpico_endpoint_id source;
    gridpico_endpoint_id destination;
    int8_t transpose_offset;
} gridpico_route_snapshot;

enum {
    GRIDPICO_PROOF_INPUTS = 1,
    GRIDPICO_PROOF_OUTPUTS = 1,
    GRIDPICO_PROOF_ROUTE_BRANCHES = 1,
    GRIDPICO_PROOF_TRANSPOSE_INSTANCES = 1,
    GRIDPICO_PROOF_NOTE_OCCURRENCES = 2,
    GRIDPICO_PROOF_RELEASE_CREDITS = 2,
    GRIDPICO_PROOF_CAPTURE_ROWS = 16
};

typedef gridpico_result (*gridpico_apply_route_fn)(
    void *context, const gridpico_route_request *request);
typedef gridpico_route_snapshot (*gridpico_read_route_fn)(void *context);
typedef gridpico_result (*gridpico_submit_note_fn)(
    void *context, const gridpico_ingress_note *input);
typedef gridpico_result (*gridpico_deliver_note_fn)(
    void *context, const gridpico_note_delivery *delivery);
typedef bool (*gridpico_transpose_note_fn)(
    int8_t offset, const gridpico_note *input, gridpico_note *output);

#endif
