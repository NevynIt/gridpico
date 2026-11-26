#ifndef GRIDPICO_PROCESSORS_MANAGED_NOTE_H
#define GRIDPICO_PROCESSORS_MANAGED_NOTE_H

#include "api/local_router.h"

bool gridpico_transform_managed_note(
    const gridpico_managed_note_transform *transform, const gridpico_note *input,
    gridpico_note *out);

#endif
