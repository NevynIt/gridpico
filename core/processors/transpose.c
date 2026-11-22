#include "transpose.h"

bool gridpico_transpose_note(
    int8_t offset, const gridpico_note *input, gridpico_note *output)
{
    const int shifted_key = (int)input->key + (int)offset;
    if (shifted_key < 0 || shifted_key > 127) {
        return false;
    }

    *output = *input;
    output->key = (uint8_t)shifted_key;
    return true;
}
