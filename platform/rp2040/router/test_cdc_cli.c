#include "bootsel_line.h"
#include "cdc_cli.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static gridpico_cdc_cli cdc;
static gridpico_cli_frontend frontend;
static unsigned executed;
static bool large_reply;

/* The executor's records are opaque to the CDC framing layer. */
gridpico_result gridpico_cli_execute_line(gridpico_cli_frontend *instance, const char *line,
                                         gridpico_cli_write_fn write, void *context)
{
    assert(instance == &frontend);
    ++executed;
    if (large_reply) {
        static const char fill[GRIDPICO_CDC_REPLY_BYTES] = {0};
        write(context, fill, sizeof(fill));
        write(context, "OK\r\n", 4);
    } else {
        write(context, line, strlen(line));
        write(context, "\r\nOK\r\n", 6);
    }
    return GRIDPICO_OK;
}

static void feed(const char *bytes, bool trace_busy)
{
    for (; *bytes != '\0'; ++bytes) gridpico_cdc_cli_feed(&cdc, &frontend, (uint8_t)*bytes, trace_busy);
}

static void expect(const char *expected)
{
    const char *result;
    const size_t count = gridpico_cdc_cli_offer(&cdc, &result);
    assert(count == strlen(expected) && memcmp(result, expected, count) == 0);
    gridpico_cdc_cli_sent(&cdc, count);
}

int main(void)
{
    feed("inv", false);
    expect("");
    feed("entory\r", false);
    expect("");
    feed("\n", false);
    const char *queued;
    assert(gridpico_cdc_cli_offer(&cdc, &queued) == 15 && memcmp(queued, "inventory", 9) == 0);
    gridpico_cdc_cli_sent(&cdc, 3);
    feed("status\nhelp\nTRACE\n", false);
    assert(executed == 1);
    expect("entory\r\nOK\r\n");
    expect("ERR UNAVAILABLE\r\n");
    expect("");

    for (unsigned i = 0; i < GRIDPICO_CLI_COMMAND_BYTES; ++i) feed("a", false);
    feed("\r\n", false);
    const size_t longest = gridpico_cdc_cli_offer(&cdc, &queued);
    assert(executed == 2 && longest == GRIDPICO_CLI_COMMAND_BYTES + 6);
    gridpico_cdc_cli_sent(&cdc, longest);
    for (unsigned i = 0; i < 700; ++i) feed("a", false);
    feed("\r\n", false);
    expect("ERR CAPACITY\r\n");
    feed("hel\rp\n", false);
    expect("ERR INVALID\r\n");
    gridpico_cdc_cli_feed(&cdc, &frontend, 0xff, false);
    feed("\n", false);
    expect("ERR INVALID\r\n");
    assert(executed == 2);

    large_reply = true;
    feed("inventory\n", false);
    expect("ERR CAPACITY\r\n");
    large_reply = false;
    feed("status\nHUBTRACE\n", true);
    assert(executed == 3);
    expect("ERR UNAVAILABLE\r\n");
    feed("sta", false);
    gridpico_cdc_cli_reset(&cdc);
    feed("help\n", false);
    expect("help\r\nOK\r\n");
    feed("status\n", false);
    gridpico_cdc_cli_reset(&cdc);
    expect("");

    gridpico_bootsel_line raw = {0};
    const char exact[] = "BOOTSEL\n";
    const char crlf[] = "BOOTSEL\r\n";
    for (size_t i = 0; i < sizeof(exact) - 1; ++i) assert(!gridpico_bootsel_feed(&raw, (uint8_t)exact[i]));
    assert(gridpico_bootsel_set_dtr(&raw, true));
    for (size_t i = 0; i < sizeof(crlf) - 1; ++i) assert(!gridpico_bootsel_feed(&raw, (uint8_t)crlf[i]));
    feed("help\n", false); /* Keep an ordinary reply pending during recovery. */
    for (size_t i = 0; i < sizeof(exact) - 1; ++i) {
        const uint8_t byte = (uint8_t)exact[i];
        const bool reboot = gridpico_bootsel_feed(&raw, byte);
        assert(reboot == (byte == '\n'));
        if (!reboot) gridpico_cdc_cli_feed(&cdc, &frontend, byte, false);
    }
    expect("help\r\nOK\r\n");
    puts("PASS CDC line limits, atomic reply, deferred busy, detach, and exact prioritized BOOTSEL");
}
