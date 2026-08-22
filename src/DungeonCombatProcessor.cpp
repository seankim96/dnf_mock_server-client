#include "DungeonCombatProcessor.h"

#include <algorithm>
#include <cmath>

namespace dnf
{
namespace
{
constexpr float DUNGEON_PLAYER_BASE_ATTACK = 40.0f;

bool IsInsideSkillHitBox(
    const Position& playerPosition,
    const Position& enemyPosition,
    float directionX,
    float directionY,
    const SkillHitBox& hitBox)
{
    const float directionLength =
        std::sqrt(directionX * directionX + directionY * directionY);
    directionX /= directionLength;
    directionY /= directionLength;

    const float offsetX = enemyPosition.x - playerPosition.x;
    const float offsetY = enemyPosition.y - playerPosition.y;
    const float forwardDistance =
        offsetX * directionX + offsetY * directionY;
    const float sideDistance =
        std::abs(offsetX * -directionY + offsetY * directionX);
    const float heightDistance =
        std::abs(enemyPosition.z - playerPosition.z);

    return forwardDistance >= -hitBox.backwardRange &&
           forwardDistance <= hitBox.forwardRange &&
           sideDistance <= hitBox.halfDepth &&
           heightDistance <= hitBox.height;
}

std::uint32_t CalculateDamage(const SkillTemplate& skill)
{
    float damageMultiplier = 0.0f;

    for (const SkillEffect& effect : skill.effects)
    {
        if (effect.type == SkillEffectType::Damage &&
            effect.target == SkillTargetType::Enemy)
        {
            damageMultiplier += effect.value;
        }
    }

    if (damageMultiplier <= 0.0f)
    {
        return 0;
    }

    return static_cast<std::uint32_t>(std::max(
        1.0f,
        std::round(DUNGEON_PLAYER_BASE_ATTACK * damageMultiplier)));
}
} // namespace

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
        pendingHits_.erase(dungeonId);
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

    ResolvePendingHits(dungeonId, *dungeon, result);

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

        pendingHits_[dungeonId].insert_or_assign(
            queuedAttack.sessionId,
            PendingSkillHit{
                *skill,
                queuedAttack.attack.directionX,
                queuedAttack.attack.directionY});
        ++result.acceptedCount;
    }

    ResolvePendingHits(dungeonId, *dungeon, result);

    return result;
}

void DungeonCombatProcessor::ResolvePendingHits(
    DungeonId dungeonId,
    DungeonInstance& dungeon,
    CombatProcessResult& result)
{
    auto dungeonHitsIt = pendingHits_.find(dungeonId);
    if (dungeonHitsIt == pendingHits_.end())
    {
        return;
    }

    auto& hits = dungeonHitsIt->second;
    for (auto hitIt = hits.begin(); hitIt != hits.end();)
    {
        const auto player = dungeon.FindPlayer(hitIt->first);
        if (player == nullptr)
        {
            hitIt = hits.erase(hitIt);
            continue;
        }

        const DungeonPlayerSnapshot playerSnapshot = player->Snapshot();
        if (!playerSnapshot.alive ||
            playerSnapshot.skillAction.skillId != hitIt->second.skill.id)
        {
            hitIt = hits.erase(hitIt);
            continue;
        }

        if (playerSnapshot.skillAction.phase != SkillActionPhase::Active)
        {
            ++hitIt;
            continue;
        }

        const auto room = dungeon.FindRoom(playerSnapshot.roomId);
        if (room != nullptr)
        {
            const std::uint32_t damage = CalculateDamage(hitIt->second.skill);

            for (const EnemyState& enemy : room->Enemies())
            {
                if (damage > 0 && enemy.alive &&
                    IsInsideSkillHitBox(
                        playerSnapshot.position,
                        enemy.position,
                        hitIt->second.directionX,
                        hitIt->second.directionY,
                        hitIt->second.skill.hitBox) &&
                    room->ApplyEnemyDamage(enemy.entityId, damage))
                {
                    ++result.hitCount;
                }
            }
        }

        hitIt = hits.erase(hitIt);
    }

    if (hits.empty())
    {
        pendingHits_.erase(dungeonHitsIt);
    }
}
} // namespace dnf
