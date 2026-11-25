#include "cdc_cli.h"

#include <string.h>

static void reset_line(gridpico_cdc_cli *cdc)
{
    cdc->line_size = 0;
    cdc->cr = cdc->invalid = cdc->overlong = false;
}

void gridpico_cdc_cli_reset(gridpico_cdc_cli *cdc)
{
    reset_line(cdc);
    cdc->reply_size = cdc->reply_sent = 0;
    cdc->overflow = cdc->declined = false;
}

static void stage(void *context, const char *bytes, size_t size)
{
    gridpico_cdc_cli *cdc = context;
    if (cdc->overflow) return;
    if (size > sizeof(cdc->reply) - cdc->reply_size) {
        cdc->overflow = true;
        return;
    }
    memcpy(cdc->reply + cdc->reply_size, bytes, size);
    cdc->reply_size = (uint16_t)(cdc->reply_size + size);
}

static void finish_line(gridpico_cdc_cli *cdc, gridpico_cli_frontend *frontend, bool trace_busy)
{
    static const char capacity[] = "ERR CAPACITY\r\n";
    static const char invalid[] = "ERR INVALID\r\n";
    if (trace_busy || cdc->reply_size != cdc->reply_sent || cdc->declined) {
        cdc->declined = true;
        return;
    }
    cdc->reply_size = cdc->reply_sent = 0;
    if (cdc->overlong) stage(cdc, capacity, sizeof(capacity) - 1);
    else if (cdc->invalid) stage(cdc, invalid, sizeof(invalid) - 1);
    else {
        cdc->line[cdc->line_size] = '\0';
        gridpico_cli_execute_line(frontend, cdc->line, stage, cdc);
        if (cdc->overflow) {
            cdc->overflow = false;
            cdc->reply_size = 0;
            stage(cdc, capacity, sizeof(capacity) - 1);
        }
    }
}

void gridpico_cdc_cli_feed(gridpico_cdc_cli *cdc, gridpico_cli_frontend *frontend, uint8_t byte, bool trace_busy)
{
    if (byte == '\n') {
        finish_line(cdc, frontend, trace_busy);
        reset_line(cdc);
        return;
    }
    if (cdc->cr) cdc->invalid = true;
    if (byte == '\r') {
        cdc->cr = true;
        return;
    }
    if (cdc->line_size == GRIDPICO_CLI_COMMAND_BYTES) {
        cdc->overlong = true;
        return;
    }
    if (byte < 0x20 || byte > 0x7e) cdc->invalid = true;
    cdc->line[cdc->line_size++] = (char)byte;
}

size_t gridpico_cdc_cli_offer(gridpico_cdc_cli *cdc, const char **bytes)
{
    static const char unavailable[] = "ERR UNAVAILABLE\r\n";
    if (cdc->reply_size == cdc->reply_sent && cdc->declined) {
        cdc->reply_size = cdc->reply_sent = 0;
        cdc->declined = false;
        stage(cdc, unavailable, sizeof(unavailable) - 1);
    }
    *bytes = cdc->reply + cdc->reply_sent;
    return (size_t)(cdc->reply_size - cdc->reply_sent);
}

void gridpico_cdc_cli_sent(gridpico_cdc_cli *cdc, size_t count)
{
    cdc->reply_sent = (uint16_t)(cdc->reply_sent + count);
}
