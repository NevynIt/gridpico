#ifndef GRIDPICO_APP_RP2040_PANIC_H
#define GRIDPICO_APP_RP2040_PANIC_H

#include "app/rp2040_router.h"

enum { GRIDPICO_PICO_PANIC_OUTPUTS = GRIDPICO_PICO_USB_CABLE_GROUPS + 1 };
enum { GRIDPICO_PICO_PANIC_NATIVE = UINT8_MAX };

typedef struct {
    gridpico_connection output;
    uint32_t lifetime, configuration;
    uint8_t index, address, interface_number, endpoint, cable, peer;
} gridpico_pico_panic_path;

typedef struct {
    uint32_t job;
    uint8_t row, channel;
} gridpico_pico_panic_ticket;

typedef enum {
    GRIDPICO_PICO_PANIC_DEFER,
    GRIDPICO_PICO_PANIC_ISSUED,
    GRIDPICO_PICO_PANIC_REFUSED,
} gridpico_pico_panic_submit;

typedef bool (*gridpico_pico_panic_resolve_fn)(gridpico_port_id, gridpico_pico_panic_path *);
typedef gridpico_pico_panic_submit (*gridpico_pico_panic_send_fn)(
    const gridpico_pico_panic_path *, const gridpico_pico_packet *, gridpico_pico_panic_ticket);

typedef struct {
    gridpico_panic_stage_report report;
    gridpico_pico_panic_path paths[GRIDPICO_PICO_PANIC_OUTPUTS];
    gridpico_pico_panic_path snapshot[GRIDPICO_PICO_PANIC_OUTPUTS];
    bool issued[GRIDPICO_PICO_PANIC_OUTPUTS];
    uint32_t job, caller, bus_epoch;
    uint8_t cursor, pending;
} gridpico_pico_panic;

gridpico_result gridpico_pico_panic_start(gridpico_pico_panic *panic,
    uint32_t caller, uint32_t bus_epoch, const gridpico_patch_inventory *inventory,
    gridpico_pico_panic_resolve_fn resolve, gridpico_panic_stage_report *out);
void gridpico_pico_panic_cancel(gridpico_pico_panic *panic);
void gridpico_pico_panic_service(gridpico_pico_panic *panic,
    gridpico_pico_panic_resolve_fn resolve, gridpico_pico_panic_send_fn send);
void gridpico_pico_panic_complete(gridpico_pico_panic *panic,
    gridpico_pico_panic_ticket ticket, bool exact);

#endif
