#pragma once

#include "DungeonManager.h"
#include "DungeonUdpManager.h"
#include "SkillCatalog.h"

#include <cstddef>
#include <unordered_map>

namespace dnf
{
struct CombatProcessResult
{
    std::size_t receivedCount = 0;
    std::size_t acceptedCount = 0;
    std::size_t rejectedCount = 0;
    std::size_t hitCount = 0;
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
    struct PendingSkillHit
    {
        SkillTemplate skill;
        float directionX = 0.0f;
        float directionY = 0.0f;
    };

    void ResolvePendingHits(
        DungeonId dungeonId,
        DungeonInstance& dungeon,
        CombatProcessResult& result);

    DungeonManager& dungeonManager_;
    DungeonUdpManager& udpManager_;
    const SkillCatalog& skillCatalog_;
    std::unordered_map<
        DungeonId,
        std::unordered_map<SessionId, PendingSkillHit>> pendingHits_;
};
} // namespace dnf
