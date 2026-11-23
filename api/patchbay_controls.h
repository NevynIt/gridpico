#ifndef GRIDPICO_PATCHBAY_CONTROLS_H
#define GRIDPICO_PATCHBAY_CONTROLS_H

#include "demo_apc_surface.h"
#include "patchbay.h"

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} gridpico_rgb8;

typedef struct {
    gridpico_revision revision;
    bool is_set;
    gridpico_rgb8 rgb;
} gridpico_device_color;

typedef gridpico_result (*gridpico_patch_read_device_color_fn)(
    void *context, gridpico_device_id device, gridpico_device_color *out);
typedef gridpico_result (*gridpico_patch_set_device_color_fn)(
    void *context, gridpico_device_id device, gridpico_rgb8 rgb);

enum {
    GRIDPICO_PATCH_LED_FRAMES = GRIDPICO_PATCH_SOURCE_ROWS * GRIDPICO_PATCH_DESTINATION_COLUMNS,
    GRIDPICO_PATCH_LED_STATUS = 0x96,
    GRIDPICO_PATCH_LED_OFF = 0x00,
    GRIDPICO_PATCH_LED_PENDING = 0x09,
    GRIDPICO_PATCH_LED_ACTIVE = 0x15,
    GRIDPICO_DIAGNOSTIC_LOG_ROWS = 16,
    GRIDPICO_CLI_COMMAND_BYTES = 63
};

typedef enum {
    GRIDPICO_DIAGNOSTIC_PORT_STATE,
    GRIDPICO_DIAGNOSTIC_ROUTE_ERROR,
    GRIDPICO_DIAGNOSTIC_NOTE_ERROR
} gridpico_diagnostic_log_kind;

typedef struct {
    uint64_t sequence;
    gridpico_diagnostic_log_kind kind;
    gridpico_port_id port;
    union {
        gridpico_patch_connection_state state;
        gridpico_result error;
    } detail;
} gridpico_diagnostic_log_entry;

typedef size_t (*gridpico_read_diagnostic_log_fn)(
    void *context, gridpico_diagnostic_log_entry *rows, size_t capacity);

#endif
