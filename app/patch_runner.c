#include "app/patch_runtime.h"
#include "app/runner_io.h"

#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    DISPATCH_BAD_FRAME,
    DISPATCH_COMPLETE,
    DISPATCH_KEYBED_EXHAUSTED,
} dispatch_status;

static const char *result_name(gridpico_result result)
{
    switch (result) {
    case GRIDPICO_OK:
        return "ok";
    case GRIDPICO_INVALID:
        return "err INVALID";
    case GRIDPICO_CAPACITY:
        return "err CAPACITY";
    case GRIDPICO_UNAVAILABLE:
        return "err UNAVAILABLE";
    }
    return "err UNAVAILABLE";
}

static const char *connection_name(gridpico_patch_connection_state state)
{
    switch (state) {
    case GRIDPICO_PATCH_CONNECTED:
        return "connected";
    case GRIDPICO_PATCH_DISCONNECTED:
        return "disconnected";
    case GRIDPICO_PATCH_AMBIGUOUS:
        return "ambiguous";
    }
    return NULL;
}

static void encode_name(char encoded[2 * GRIDPICO_PATCH_NAME_BYTES - 1],
                        const char name[GRIDPICO_PATCH_NAME_BYTES])
{
    const char hex[] = "0123456789ABCDEF";
    if (name[0] == '\0') {
        memcpy(encoded, "-", sizeof("-"));
        return;
    }
    size_t i = 0;
    for (; i < GRIDPICO_PATCH_NAME_BYTES - 1 && name[i] != '\0'; ++i) {
        const unsigned byte = (unsigned char)name[i];
        encoded[2 * i] = hex[byte >> 4];
        encoded[2 * i + 1] = hex[byte & 15u];
    }
    encoded[2 * i] = '\0';
}

static size_t port_slot(const gridpico_patch_snapshot *snapshot, const gridpico_patch_port *port)
{
    const bool input = port->direction == GRIDPICO_PATCH_INPUT;
    const gridpico_port_id *slots = input ? snapshot->source_rows : snapshot->destination_columns;
    const size_t count = input ? GRIDPICO_PATCH_SOURCE_ROWS : GRIDPICO_PATCH_DESTINATION_COLUMNS;
    for (size_t i = 0; i < count; ++i) {
        if (slots[i] == port->id) {
            return i + 1;
        }
    }
    return 0;
}

static bool write_inventory(const gridpico_patch_snapshot *snapshot)
{
    const gridpico_patch_inventory *inventory = &snapshot->inventory;
    if (printf("inventory %" PRIu64 " %zu %zu\n", snapshot->revision,
               inventory->device_count, inventory->port_count) < 0) {
        return false;
    }
    char encoded[2 * GRIDPICO_PATCH_NAME_BYTES - 1];
    for (size_t i = 0; i < inventory->device_count; ++i) {
        const gridpico_patch_device *device = &inventory->devices[i];
        const char *state = connection_name(device->state);
        encode_name(encoded, device->name);
        if (state == NULL || printf("device %u %s %s\n", (unsigned)device->id, state, encoded) < 0) {
            return false;
        }
    }
    for (size_t i = 0; i < inventory->port_count; ++i) {
        const gridpico_patch_port *port = &inventory->ports[i];
        const char *state = connection_name(port->state);
        const char *direction = port->direction == GRIDPICO_PATCH_INPUT ? "in" : "out";
        encode_name(encoded, port->name);
        if (state == NULL ||
            printf("port %u %u %s %s %u %zu %s\n", (unsigned)port->id, (unsigned)port->device,
                   direction, state, (unsigned)port->patch_eligible,
                   port_slot(snapshot, port), encoded) < 0) {
            return false;
        }
    }
    return puts("inventory-end") != EOF;
}

static bool write_notes(const patch_output *output)
{
    for (size_t i = 0; i < output->note_count; ++i) {
        if (!write_note(output->notes[i].note)) {
            return false;
        }
    }
    return true;
}

static bool write_page(const patch_output *output)
{
    if (!output->has_page) {
        return true;
    }
    const gridpico_patch_led_page page = output->page;
    if (page.layer == GRIDPICO_PATCH_LED_LAYER_ROUTE) {
        if (puts("led-page route") == EOF) {
            return false;
        }
    } else if (page.layer != GRIDPICO_PATCH_LED_LAYER_DEVICE_COLOR || page.device == 0 ||
               printf("led-page device-color %u\n", (unsigned)page.device) < 0) {
        return false;
    }
    for (size_t i = 0; i < output->led_count; ++i) {
        if (!write_surface(output->leds[i])) {
            return false;
        }
    }
    return true;
}

static bool write_response(const patch_output *output, const gridpico_patch_snapshot *inventory,
                           gridpico_result result)
{
    if (output->overflowed || (inventory != NULL && !output->has_page) ||
        output->led_count != (output->has_page ? GRIDPICO_PATCH_LED_FRAMES : 0u)) {
        fputs("Patch runner could not stage a complete dispatch\n", stderr);
        return false;
    }
    return write_notes(output) && (inventory == NULL || write_inventory(inventory)) &&
           write_page(output) && puts(result_name(result)) != EOF && fflush(stdout) == 0;
}

static dispatch_status dispatch(patch_runtime *runtime, const char *line,
                                gridpico_patch_snapshot *snapshot, bool *inventory,
                                gridpico_result *result)
{
    patch_runtime_clear_output(runtime);
    *inventory = false;
    if (strcmp(line, "surface-ready") == 0) {
        *result = patch_runtime_refresh(runtime, snapshot);
        *inventory = *result == GRIDPICO_OK;
        if (!*inventory) {
            patch_runtime_clear_output(runtime);
        }
        return DISPATCH_COMPLETE;
    }
    const char surface_prefix[] = "surface ";
    if (strncmp(line, surface_prefix, sizeof(surface_prefix) - 1) == 0) {
        gridpico_apc_surface_message message;
        if (!parse_surface(line + sizeof(surface_prefix) - 1, &message)) {
            return DISPATCH_BAD_FRAME;
        }
        *result = gridpico_apc_patch_receive(&runtime->apc, &message);
        return DISPATCH_COMPLETE;
    }
    gridpico_ingress_note input = {.source = runtime->keybed};
    if (!parse_note(line, &input.note)) {
        return DISPATCH_BAD_FRAME;
    }
    *result = patch_runtime_submit(runtime, &input);
    return input.note.kind == GRIDPICO_NOTE_ON && *result == GRIDPICO_CAPACITY
        ? DISPATCH_KEYBED_EXHAUSTED : DISPATCH_COMPLETE;
}

int main(void)
{
#ifdef SIGPIPE
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        fputs("Patch runner could not ignore SIGPIPE\n", stderr);
        return EXIT_FAILURE;
    }
#endif
    patch_runtime runtime;
    if (patch_runtime_init_browser(&runtime) != GRIDPICO_OK) {
        fputs("Patch runner could not initialize the fixture\n", stderr);
        return EXIT_FAILURE;
    }

    bool output_ok = puts("ready") != EOF && fflush(stdout) == 0;
    bool malformed = false;
    bool keybed_exhausted = false;
    gridpico_result cleanup = GRIDPICO_OK;
    while (output_ok && !keybed_exhausted) {
        char line[REQUEST_SIZE];
        const request_status status = read_request(line);
        if (status == REQUEST_EOF) {
            break;
        }
        gridpico_patch_snapshot snapshot;
        bool inventory;
        gridpico_result result;
        const dispatch_status dispatched = status == REQUEST_LINE
            ? dispatch(&runtime, line, &snapshot, &inventory, &result)
            : DISPATCH_BAD_FRAME;
        if (dispatched == DISPATCH_BAD_FRAME) {
            fputs("Patch runner received malformed framing\n", stderr);
            malformed = true;
            break;
        }
        keybed_exhausted = dispatched == DISPATCH_KEYBED_EXHAUSTED;
        if (keybed_exhausted) {
            /* Keep all accepted releases, including any accepted on teardown retry. */
            cleanup = patch_runtime_disconnect_keybed(&runtime);
        }
        output_ok = write_response(&runtime.output, inventory ? &snapshot : NULL, result);
    }

    if (!keybed_exhausted) {
        patch_runtime_clear_output(&runtime);
        cleanup = patch_runtime_disconnect_keybed(&runtime);
        output_ok = output_ok && write_notes(&runtime.output) && fflush(stdout) == 0;
    }
    if (cleanup != GRIDPICO_OK || ferror(stdin) || !output_ok) {
        fputs("Patch runner could not complete stdio or disconnect cleanup\n", stderr);
        return EXIT_FAILURE;
    }
    return malformed ? EXIT_FAILURE : EXIT_SUCCESS;
}
