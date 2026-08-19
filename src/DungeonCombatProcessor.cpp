#include "DungeonCombatProcessor.h"

namespace dnf
{
DungeonCombatProcessor::DungeonCombatProcessor(
    DungeonManager& dungeonManager,
    DungeonUdpManager& udpManager,
    const SkillCatalog& skillCatalog)
    : dungeonManager_(dungeonManager),
      udpManager_(udpManager),
      skillCatalog_(skillCatalog)
{
}

CombatProcessResult DungeonCombatProcessor::Process(DungeonId dungeonId)
{
    CombatProcessResult result;

    const auto dungeon = dungeonManager_.FindDungeon(dungeonId);
    if (dungeon == nullptr || dungeon->State() != DungeonState::Running)
    {
        return result;
    }

    for (SessionId sessionId : dungeon->Participants())
    {
        const auto player = dungeon->FindPlayer(sessionId);
        if (player != nullptr)
        {
            player->AdvanceCombatTick();
        }
    }

    AuthenticatedPlayerAttack queuedAttack;
    while (udpManager_.TryPopAttack(dungeonId, queuedAttack))
    {
        ++result.receivedCount;

        const auto player = dungeon->FindPlayer(queuedAttack.sessionId);
        const auto skill =
            skillCatalog_.GetSkill(queuedAttack.attack.skillId);

        if (player == nullptr || !skill.has_value())
        {
            ++result.rejectedCount;
            continue;
        }

        const BeginSkillResult beginResult = player->BeginSkill(
            skill->id,
            skill->manaCost,
            skill->cooldownTicks,
            skill->startupTicks,
            skill->activeTicks,
            skill->recoveryTicks);
        if (beginResult != BeginSkillResult::Success)
        {
            ++result.rejectedCount;
            continue;
        }

        ++result.acceptedCount;
    }

    return result;
}
} // namespace dnf
