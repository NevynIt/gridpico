#ifndef GRIDPICO_RP2040_CDC_CLI_H
#define GRIDPICO_RP2040_CDC_CLI_H

#include "frontends/cli/cli_frontend.h"

enum { GRIDPICO_CDC_REPLY_BYTES = 4096 };

typedef struct {
    char line[GRIDPICO_CLI_LINE_BYTES];
    char reply[GRIDPICO_CDC_REPLY_BYTES];
    uint16_t reply_size, reply_sent;
    uint8_t line_size;
    bool cr, invalid, overlong, overflow, declined;
} gridpico_cdc_cli;

void gridpico_cdc_cli_reset(gridpico_cdc_cli *cdc);
void gridpico_cdc_cli_feed(gridpico_cdc_cli *cdc, gridpico_cli_frontend *frontend, uint8_t byte, bool trace_busy);
/* Call only when no trace owns the stream. A pending refusal follows the preceding reply. */
size_t gridpico_cdc_cli_offer(gridpico_cdc_cli *cdc, const char **bytes);
void gridpico_cdc_cli_sent(gridpico_cdc_cli *cdc, size_t count);

#endif
