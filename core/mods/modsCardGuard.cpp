// Modular-Trackular mods. SPDX-License-Identifier: MIT

#include "modsCardGuard.h"

#ifndef MT_HOST_TEST

#include "fileManager.h"
#include "mtSequencer.h"

namespace mods
{

bool cardFreeOfPatternLoad()
{
    if (newFileManager.isPatternLoading())
    {
        return false;
    }

    const auto* player = sequencer.ptrPlayer;
    if (!player->isPlay)
    {
        return true;
    }
    if (!player->songMode && !player->sequencialSwitch_isArmed)
    {
        return true; // no load at the pattern end
    }
    if (player->performanceMode || player->selectionMode)
    {
        return false;
    }
    return !loadRequestNear(sequencer.getActualPattern()->track[0].length,
                            sequencer.getActualPos(),
                            sequencer.getCurrentTempo(),
                            kLoadGuardMs);
}

} // namespace mods

#endif // MT_HOST_TEST
