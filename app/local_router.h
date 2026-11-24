#ifndef GRIDPICO_APP_LOCAL_ROUTER_H
#define GRIDPICO_APP_LOCAL_ROUTER_H

#include "app/local_runtime.h"

typedef struct {
    void *context;
    /* Service one adapter event. Set handled when the current dispatch has work. */
    gridpico_result (*next)(void *context, local_runtime *runtime, bool *handled);
    int interval_ms;
} local_router_service;

int local_router_run(local_runtime *runtime, const local_router_service *service, bool ui_descriptor);

#endif
