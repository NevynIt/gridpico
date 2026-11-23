#ifndef GRIDPICO_APP_RUNNER_IO_H
#define GRIDPICO_APP_RUNNER_IO_H

#include "api/demo_apc_surface.h"

enum { REQUEST_SIZE = sizeof("surface 239 127 127") };

typedef enum {
    REQUEST_EOF,
    REQUEST_LINE,
    REQUEST_MALFORMED
} request_status;

request_status read_request(char line[REQUEST_SIZE]);
bool parse_note(const char *line, gridpico_note *note);
bool parse_surface(const char *line, gridpico_apc_surface_message *message);
bool write_note(gridpico_note note);
bool write_surface(gridpico_apc_surface_message message);

#endif
