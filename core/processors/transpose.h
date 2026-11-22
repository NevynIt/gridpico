#ifndef GRIDPICO_PROCESSORS_TRANSPOSE_H
#define GRIDPICO_PROCESSORS_TRANSPOSE_H

#include "api/native_proof.h"

/* Input is a normalized note and offset is in [-127, 127].
 * Returns false and leaves output untouched when the shifted key is out of range.
 */
bool gridpico_transpose_note(
    int8_t offset, const gridpico_note *input, gridpico_note *output);

#endif
