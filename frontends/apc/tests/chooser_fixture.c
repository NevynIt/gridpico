/* C-only fixture. From the repository root:
 * cc -std=c11 -Wall -Wextra -Wpedantic -Werror -I. \
 *   frontends/apc/tests/chooser_fixture.c frontends/apc/apc_chooser.c \
 *   frontends/apc/apc_protocol.c -o /tmp/gridpico-apc-chooser
 */
#include "frontends/apc/apc_chooser.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const gridpico_connection apc = {3, 7};

static void send(gridpico_apc_chooser *chooser, gridpico_connection from,
                 uint8_t cable, uint8_t status, uint8_t data, uint8_t value,
                 bool consumed, gridpico_apc_chooser_mode mode, bool visible)
{
    uint8_t bytes[] = {status, data, value};
    const gridpico_apc_chooser_result result =
        gridpico_apc_chooser_receive(chooser, from, cable, bytes, sizeof(bytes));
    assert(result.consumed == consumed && result.requested_mode == mode);
    assert(chooser->open == visible);
    const uint8_t expected[] = {status, data, value};
    assert(memcmp(bytes, expected, sizeof(expected)) == 0);
}

static void ordinary(gridpico_apc_chooser *chooser, uint8_t cable, uint8_t status,
                     uint8_t data, uint8_t value, bool visible)
{
    send(chooser, apc, cable, status, data, value,
         false, GRIDPICO_APC_CHOOSER_NO_REQUEST, visible);
}

static void local(gridpico_apc_chooser *chooser, uint8_t cable, uint8_t status,
                  uint8_t data, uint8_t value, bool visible)
{
    send(chooser, apc, cable, status, data, value,
         true, GRIDPICO_APC_CHOOSER_NO_REQUEST, visible);
}

static void start(gridpico_apc_chooser *chooser)
{
    ordinary(chooser, 1, 0x90, 0x62, 0x7f, false);
    local(chooser, 0, 0xb0, 0x40, 0x40, true);
}

static void ordinary_and_reversed(void)
{
    gridpico_apc_chooser chooser;
    gridpico_apc_chooser_init(&chooser, apc);
    ordinary(&chooser, 1, 0x90, 0x62, 0x7f, false);
    ordinary(&chooser, 1, 0x90, 0x62, 0x00, false);
    ordinary(&chooser, 0, 0xb0, 0x40, 0x40, false);
    ordinary(&chooser, 1, 0x90, 0x62, 0x7f, false);
    ordinary(&chooser, 0, 0xb0, 0x40, 0x7f, false);
    ordinary(&chooser, 0, 0xb0, 0x40, 0x3f, false);
    ordinary(&chooser, 1, 0x80, 0x62, 0x20, false);
    ordinary(&chooser, 1, 0x80, 0x20, 0x20, false);

    const uint8_t pressure[] = {0xd0, 0x7f};
    const gridpico_apc_chooser_result result =
        gridpico_apc_chooser_receive(&chooser, apc, 0, pressure, sizeof(pressure));
    assert(!result.consumed && result.requested_mode == GRIDPICO_APC_CHOOSER_NO_REQUEST);
}

static void one_choice_and_release_ownership(void)
{
    gridpico_apc_chooser chooser;
    gridpico_apc_chooser_init(&chooser, apc);
    ordinary(&chooser, 0, 0x90, 0x3c, 0x60, false);
    ordinary(&chooser, 1, 0x90, 0x21, 0x7f, false);
    start(&chooser);
    ordinary(&chooser, 1, 0x90, 0x21, 0x7f, true);
    ordinary(&chooser, 1, 0x80, 0x21, 0x20, true);
    send(&chooser, apc, 1, 0x90, 0x21, 0x01, true, GRIDPICO_APC_CHOOSER_KEYS, false);
    ordinary(&chooser, 0, 0x80, 0x3c, 0x20, false);
    local(&chooser, 1, 0x90, 0x21, 0x7f, false);
    ordinary(&chooser, 1, 0x90, 0x20, 0x7f, false);
    ordinary(&chooser, 1, 0x80, 0x20, 0x00, false);
    ordinary(&chooser, 1, 0x80, 0x62, 0x20, false);
    local(&chooser, 0, 0xb0, 0x40, 0x7f, false);
    ordinary(&chooser, 1, 0x90, 0x62, 0x7f, false);
    local(&chooser, 0, 0xb0, 0x40, 0x3f, false);
    ordinary(&chooser, 0, 0xb0, 0x40, 0x7f, false);
    ordinary(&chooser, 0, 0xb0, 0x40, 0x00, false);
    ordinary(&chooser, 1, 0x90, 0x62, 0x00, false);

    start(&chooser);
    local(&chooser, 1, 0x90, 0x21, 0x7f, true);
    local(&chooser, 1, 0x90, 0x21, 0x00, true);
    send(&chooser, apc, 1, 0x90, 0x20, 0x7f, true, GRIDPICO_APC_CHOOSER_PATCHBAY, false);
    local(&chooser, 1, 0x80, 0x20, 0x29, false);
    local(&chooser, 0, 0xb0, 0x40, 0x00, false);
    ordinary(&chooser, 1, 0x80, 0x62, 0x00, false);
}

static void cancel_and_attachment(void)
{
    const gridpico_connection previous = {3, 6};
    const gridpico_connection other = {4, 7};
    gridpico_apc_chooser chooser;
    gridpico_apc_chooser_init(&chooser, apc);
    send(&chooser, previous, 1, 0x90, 0x62, 0x7f, false, GRIDPICO_APC_CHOOSER_NO_REQUEST, false);
    ordinary(&chooser, 0, 0xb0, 0x40, 0x7f, false);
    ordinary(&chooser, 0, 0xb0, 0x40, 0x00, false);
    ordinary(&chooser, 0, 0x90, 0x62, 0x7f, false);
    ordinary(&chooser, 1, 0x91, 0x62, 0x7f, false);
    ordinary(&chooser, 1, 0x90, 0x62, 0x7f, false);
    send(&chooser, other, 0, 0xb0, 0x40, 0x7f, false, GRIDPICO_APC_CHOOSER_NO_REQUEST, false);
    ordinary(&chooser, 1, 0xb0, 0x40, 0x7f, false);
    ordinary(&chooser, 0, 0xb1, 0x40, 0x7f, false);
    ordinary(&chooser, 0, 0xb0, 0x40, 0xff, false);
    local(&chooser, 0, 0xb0, 0x40, 0x7f, true);
    ordinary(&chooser, 0, 0x90, 0x20, 0x7f, true);
    ordinary(&chooser, 1, 0x90, 0x22, 0x7f, true);
    gridpico_apc_chooser_detach(&chooser, previous);
    assert(chooser.open);
    local(&chooser, 0, 0xb0, 0x40, 0x00, false);
    ordinary(&chooser, 1, 0x80, 0x62, 0x00, false);
    start(&chooser);
    ordinary(&chooser, 1, 0x90, 0x62, 0x00, false);
    local(&chooser, 0, 0xb0, 0x40, 0x00, false);
    start(&chooser);
    gridpico_apc_chooser_detach(&chooser, apc);
    assert(!chooser.open);
    ordinary(&chooser, 0, 0xb0, 0x40, 0x00, false);

    const gridpico_connection replacement = {3, 8};
    gridpico_apc_chooser_init(&chooser, replacement);
    ordinary(&chooser, 1, 0x90, 0x62, 0x7f, false);
    send(&chooser, replacement, 0, 0xb0, 0x40, 0x7f,
         false, GRIDPICO_APC_CHOOSER_NO_REQUEST, false);
    send(&chooser, replacement, 0, 0xb0, 0x40, 0x00,
         false, GRIDPICO_APC_CHOOSER_NO_REQUEST, false);
    send(&chooser, replacement, 1, 0x90, 0x62, 0x7f,
         false, GRIDPICO_APC_CHOOSER_NO_REQUEST, false);
    send(&chooser, replacement, 0, 0xb0, 0x40, 0x7f,
         true, GRIDPICO_APC_CHOOSER_NO_REQUEST, true);
}

int main(void)
{
    ordinary_and_reversed();
    one_choice_and_release_ownership();
    cancel_and_attachment();
    puts("APC chooser fixture passed: ordered chord, one choice, ownership and attachment");
    return 0;
}
