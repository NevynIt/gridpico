#ifndef GRIDPICO_APP_LOCAL_PROFILE_H
#define GRIDPICO_APP_LOCAL_PROFILE_H

#include "api/local_router.h"

enum { LOCAL_MIDI_QUEUE = 64, LOCAL_PANEL_QUEUE = 128 };

typedef enum {
    LOCAL_ROLE_AUX,
    LOCAL_ROLE_KEYBED,
    LOCAL_ROLE_PANEL,
    LOCAL_ROLE_LED,
    LOCAL_ROLE_SEQUENCE,
    LOCAL_ROLE_MONITOR,
    LOCAL_ROLE_LOG,
    LOCAL_ROLE_COUNT,
} local_port_role;

typedef struct {
    local_port_role role;
    unsigned queue_limit;
    bool socket;
} local_profile_port;

typedef struct {
    gridpico_local_registry registry;
    gridpico_local_profile limits;
    local_profile_port ports[GRIDPICO_PATCH_MAX_PORTS];
    bool passive_surface;
    bool note_transform;
} local_router_profile;

bool local_profile_load(const char *path, local_router_profile *profile);
bool local_profile_supports_web_ui(const local_router_profile *profile);
gridpico_port_id local_profile_role(const local_router_profile *profile, local_port_role role);
const char *local_profile_role_token(local_port_role role);

#endif
