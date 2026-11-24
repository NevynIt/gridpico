#include "apc_chooser.h"

#include "apc_protocol.h"

enum {
    APC_KEYBED_CABLE = 0,
    APC_PANEL_CABLE = 1,
    APC_CONTROL_CHANGE = 0xb0,
    APC_SUSTAIN = 0x40,
    APC_SUSTAIN_ON = 64,
    APC_SHIFT = 0x62,
    APC_FIRST_SELECTOR = 0x20
};

static bool matches_attachment(const gridpico_apc_chooser *chooser, gridpico_connection attachment)
{
    return attachment.endpoint != 0 && attachment.generation != 0 &&
           chooser->attachment.endpoint == attachment.endpoint &&
           chooser->attachment.generation == attachment.generation;
}

void gridpico_apc_chooser_init(gridpico_apc_chooser *chooser, gridpico_connection attachment)
{
    *chooser = (gridpico_apc_chooser){.attachment = attachment};
}

void gridpico_apc_chooser_detach(gridpico_apc_chooser *chooser, gridpico_connection attachment)
{
    if (matches_attachment(chooser, attachment)) {
        *chooser = (gridpico_apc_chooser){0};
    }
}

static void end_chord(gridpico_apc_chooser *chooser)
{
    chooser->open = false;
    if (!chooser->shift_down && chooser->sustain == GRIDPICO_APC_CHOOSER_UP) {
        chooser->chord_used = false;
    }
}

static gridpico_apc_chooser_result receive_sustain(gridpico_apc_chooser *chooser, bool pressed)
{
    if (!pressed) {
        const bool consumed = chooser->sustain == GRIDPICO_APC_CHOOSER_LOCAL_DOWN;
        chooser->sustain = GRIDPICO_APC_CHOOSER_UP;
        end_chord(chooser);
        return (gridpico_apc_chooser_result){.consumed = consumed};
    }
    if (chooser->sustain == GRIDPICO_APC_CHOOSER_UP) {
        if (chooser->shift_down && !chooser->chord_used) {
            chooser->sustain = GRIDPICO_APC_CHOOSER_LOCAL_DOWN;
            chooser->chord_used = true;
            chooser->open = true;
        } else {
            chooser->sustain = GRIDPICO_APC_CHOOSER_ORDINARY_DOWN;
        }
    }
    return (gridpico_apc_chooser_result){
        .consumed = chooser->sustain == GRIDPICO_APC_CHOOSER_LOCAL_DOWN
    };
}

static gridpico_apc_chooser_result receive_selector(
    gridpico_apc_chooser *chooser, size_t selector, bool pressed)
{
    gridpico_apc_chooser_press *hold = &chooser->selectors[selector];
    if (!pressed) {
        const bool consumed = *hold == GRIDPICO_APC_CHOOSER_LOCAL_DOWN;
        *hold = GRIDPICO_APC_CHOOSER_UP;
        return (gridpico_apc_chooser_result){.consumed = consumed};
    }
    if (*hold != GRIDPICO_APC_CHOOSER_UP) {
        return (gridpico_apc_chooser_result){.consumed = *hold == GRIDPICO_APC_CHOOSER_LOCAL_DOWN};
    }
    if (chooser->open) {
        *hold = GRIDPICO_APC_CHOOSER_LOCAL_DOWN;
        chooser->open = false;
        return (gridpico_apc_chooser_result){
            .consumed = true,
            .requested_mode = selector == 0 ? GRIDPICO_APC_CHOOSER_PATCHBAY : GRIDPICO_APC_CHOOSER_KEYS
        };
    }
    *hold = GRIDPICO_APC_CHOOSER_ORDINARY_DOWN;
    return (gridpico_apc_chooser_result){0};
}

gridpico_apc_chooser_result gridpico_apc_chooser_receive(
    gridpico_apc_chooser *chooser, gridpico_connection attachment,
    uint8_t cable, const uint8_t *bytes, size_t byte_count)
{
    if (!matches_attachment(chooser, attachment) || bytes == NULL || byte_count != 3) {
        return (gridpico_apc_chooser_result){0};
    }
    if (cable == APC_KEYBED_CABLE && bytes[0] == APC_CONTROL_CHANGE &&
        bytes[1] == APC_SUSTAIN && bytes[2] < 0x80) {
        return receive_sustain(chooser, bytes[2] >= APC_SUSTAIN_ON);
    }
    if (cable == APC_PANEL_CABLE) {
        const gridpico_apc_surface_message message = {{bytes[0], bytes[1], bytes[2]}};
        uint8_t note = 0;
        const gridpico_apc_pad_event event = gridpico_apc_decode_button(&message, &note);
        if (event == GRIDPICO_APC_PAD_PRESSED || event == GRIDPICO_APC_PAD_RELEASED) {
            const bool pressed = event == GRIDPICO_APC_PAD_PRESSED;
            if (note == APC_SHIFT) {
                chooser->shift_down = pressed;
                if (!pressed) {
                    end_chord(chooser);
                }
            } else if (note == APC_FIRST_SELECTOR || note == APC_FIRST_SELECTOR + 1) {
                return receive_selector(chooser, (size_t)(note - APC_FIRST_SELECTOR), pressed);
            }
        }
    }
    return (gridpico_apc_chooser_result){0};
}
