#include "app/patch_runtime.h"

#include <assert.h>
#include <string.h>

typedef struct {
    char output[8192];
    size_t size;
} transcript;

static void capture(void *context, const char *bytes, size_t size)
{
    transcript *out = context;
    assert(size < sizeof out->output - out->size);
    memcpy(out->output + out->size, bytes, size);
    out->size += size;
    out->output[out->size] = '\0';
}

static void clear(transcript *out)
{
    out->size = 0;
    out->output[0] = '\0';
}

static bool contains(const transcript *out, const char *expected)
{
    const size_t expected_size = strlen(expected);
    for (size_t i = 0; i + expected_size <= out->size; ++i) {
        if (memcmp(out->output + i, expected, expected_size) == 0) {
            return true;
        }
    }
    return false;
}

static bool contains_bytes(const transcript *out, const char *expected, size_t expected_size)
{
    for (size_t i = 0; i + expected_size <= out->size; ++i) {
        if (memcmp(out->output + i, expected, expected_size) == 0) {
            return true;
        }
    }
    return false;
}

static void compare_terminal_reply(gridpico_cli_frontend *cli, transcript *terminal,
                                   const char *line,
                                   gridpico_result expected_result)
{
    transcript diagnostics = {0};
    assert(gridpico_cli_execute_line(cli, line, capture, &diagnostics) == expected_result);
    clear(terminal);
    gridpico_cli_receive(cli, line, strlen(line));
    gridpico_cli_receive(cli, "\r\n", 2);
    gridpico_cli_process(cli);
    assert(contains_bytes(terminal, diagnostics.output, diagnostics.size));
}

int main(void)
{
    patch_runtime runtime;
    assert(patch_runtime_init(&runtime) == GRIDPICO_OK);

    transcript terminal = {0};
    gridpico_cli_frontend cli;
    assert(patch_runtime_bind_cli(&runtime, &cli, capture, &terminal) == GRIDPICO_OK);

    const gridpico_patch_route_request route = {
        .source = 1,
        .destination = 2,
        .enabled = true,
    };
    assert(patch_runtime_set_route(&runtime, &route) == GRIDPICO_OK);
    const gridpico_patch_port_update disconnect = {
        .connection = {.endpoint = 2, .generation = 1},
        .state = GRIDPICO_PATCH_DISCONNECTED,
    };
    assert(patch_runtime_update_port(&runtime, &disconnect) == GRIDPICO_OK);

    assert(gridpico_cli_execute_line(&cli, "inventory", capture, &terminal) == GRIDPICO_OK);
    assert(contains(&terminal, "DEVICE 1 connected unset \"APC Key 25 mk2\"\r\n"));
    assert(contains(&terminal, "PORT 1 1 in connected row:1:1 \"Keybed\"\r\n"));
    assert(contains(&terminal, "PORT 4 1 in connected - \"Panel\"\r\n"));
    assert(contains(&terminal, "PORT 5 1 out connected - \"LED feedback\"\r\n"));
    assert(contains(&terminal, "DEVICE 2 connected unset \"Simulated dual-port instrument\"\r\n"));
    assert(contains(&terminal, "PORT 2 2 out disconnected col:1:1 \"MIDI A\"\r\n"));
    assert(contains(&terminal, "PORT 3 2 out connected col:1:2 \"MIDI B\"\r\nOK\r\n"));

    clear(&terminal);
    assert(gridpico_cli_execute_line(&cli, "color get 2", capture, &terminal) == GRIDPICO_OK);
    assert(strcmp(terminal.output, "COLOR 2 unset\r\nOK\r\n") == 0);

    clear(&terminal);
    assert(gridpico_cli_execute_line(&cli, "color set 2 #ff00a2", capture, &terminal) == GRIDPICO_OK);
    assert(strcmp(terminal.output, "COLOR 2 #FF00A2\r\nOK\r\n") == 0);
    gridpico_device_color color;
    assert(gridpico_session_read_device_color(&runtime.session, 2, &color) == GRIDPICO_OK);
    assert(color.revision == 1 && color.is_set);

    clear(&terminal);
    assert(gridpico_cli_execute_line(&cli, "inventory", capture, &terminal) == GRIDPICO_OK);
    assert(contains(&terminal, "DEVICE 2 connected #FF00A2"));

    clear(&terminal);
    assert(gridpico_cli_execute_line(&cli, "status", capture, &terminal) == GRIDPICO_OK);
    assert(strcmp(terminal.output, "STATUS 2 2 5 0 1\r\nOK\r\n") == 0);

    const gridpico_patch_route_request invalid_route = {
        .source = 99,
        .destination = 2,
        .enabled = true,
    };
    assert(patch_runtime_set_route(&runtime, &invalid_route) == GRIDPICO_INVALID);
    clear(&terminal);
    assert(gridpico_cli_execute_line(&cli, "logs", capture, &terminal) == GRIDPICO_OK);
    assert(strcmp(terminal.output,
                  "LOG 1 PORT 2 disconnected\r\n"
                  "LOG 2 ROUTE_ERROR 2 INVALID\r\nOK\r\n") == 0);

    clear(&terminal);
    assert(gridpico_cli_execute_line(&cli, "color get 99", capture, &terminal) == GRIDPICO_INVALID);
    assert(strcmp(terminal.output, "ERR INVALID\r\n") == 0);

    compare_terminal_reply(&cli, &terminal, "inventory", GRIDPICO_OK);
    compare_terminal_reply(&cli, &terminal, "status", GRIDPICO_OK);
    compare_terminal_reply(&cli, &terminal, "logs", GRIDPICO_OK);
    compare_terminal_reply(&cli, &terminal, "color get 2", GRIDPICO_OK);
    compare_terminal_reply(&cli, &terminal, "color set 2 #FF00A2", GRIDPICO_OK);

    clear(&terminal);
    gridpico_cli_receive(&cli, "inventory\r\n", sizeof("inventory\r\n") - 1);
    gridpico_cli_process(&cli);
    assert(contains(&terminal, "OK\r\n"));
    clear(&terminal);
    gridpico_cli_receive(&cli, "inventory\n", sizeof("inventory\n") - 1);
    gridpico_cli_process(&cli);
    assert(contains(&terminal, "OK\r\n"));
    clear(&terminal);
    gridpico_cli_receive(&cli, "inventory\r", sizeof("inventory\r") - 1);
    gridpico_cli_process(&cli);
    assert(contains(&terminal, "OK\r\n"));

    clear(&terminal);
    char long_line[GRIDPICO_CLI_LINE_BYTES + 1];
    memset(long_line, 'x', sizeof long_line - 1);
    long_line[sizeof long_line - 1] = '\0';
    assert(gridpico_cli_execute_line(&cli, long_line, capture, &terminal) == GRIDPICO_CAPACITY);
    assert(strcmp(terminal.output, "ERR CAPACITY\r\n") == 0);
    clear(&terminal);
    gridpico_cli_receive(&cli, long_line, sizeof long_line - 1);
    gridpico_cli_receive(&cli, "\r", 1);
    gridpico_cli_process(&cli);
    assert(contains(&terminal, "ERR CAPACITY\r\n"));
    return 0;
}
