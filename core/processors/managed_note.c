#include "managed_note.h"

#include "transpose.h"

bool gridpico_transform_managed_note(
    const gridpico_managed_note_transform *transform, const gridpico_note *input,
    gridpico_note *out)
{
    if (!gridpico_transpose_note(transform->transpose_semitones, input, out)) {
        return false;
    }

    if (transform->output_channel != 0) {
        out->channel_index = (uint8_t)(transform->output_channel - 1);
    }
    return true;
}
