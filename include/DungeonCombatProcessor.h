#pragma once

#include "DungeonManager.h"
#include "DungeonUdpManager.h"
#include "SkillCatalog.h"

#include <cstddef>

namespace dnf
{
struct CombatProcessResult
{
    std::size_t receivedCount = 0;
    std::size_t acceptedCount = 0;
    std::size_t rejectedCount = 0;
};

class DungeonCombatProcessor
{
public:
    DungeonCombatProcessor(
        DungeonManager& dungeonManager,
        DungeonUdpManager& udpManager,
        const SkillCatalog& skillCatalog);

    CombatProcessResult Process(DungeonId dungeonId);

private:
    DungeonManager& dungeonManager_;
    DungeonUdpManager& udpManager_;
    const SkillCatalog& skillCatalog_;
};
} // namespace dnf
