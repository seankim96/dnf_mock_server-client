#pragma once

#include "DungeonManager.h"
#include "DungeonUdpManager.h"

namespace dnf
{
class DungeonLifecycleService
{
public:
    DungeonLifecycleService(
        DungeonManager& dungeonManager,
        DungeonUdpManager& udpManager);

    bool CancelWaitingDungeon(DungeonId dungeonId);
    bool FinishDungeon(DungeonId dungeonId);

private:
    DungeonManager& dungeonManager_;
    DungeonUdpManager& udpManager_;
};
} // namespace dnf
