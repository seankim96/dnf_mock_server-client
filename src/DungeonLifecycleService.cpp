#include "DungeonLifecycleService.h"

namespace dnf
{
DungeonLifecycleService::DungeonLifecycleService(
    DungeonManager& dungeonManager,
    DungeonUdpManager& udpManager)
    : dungeonManager_(dungeonManager),
      udpManager_(udpManager)
{
}

bool DungeonLifecycleService::CancelWaitingDungeon(DungeonId dungeonId)
{
    if (!dungeonManager_.CancelDungeon(dungeonId))
    {
        return false;
    }

    udpManager_.Release(dungeonId);
    return true;
}

bool DungeonLifecycleService::FinishDungeon(DungeonId dungeonId)
{
    if (!dungeonManager_.FinishDungeon(dungeonId))
    {
        return false;
    }

    udpManager_.Release(dungeonId);
    return true;
}
} // namespace dnf
