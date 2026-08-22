#include "DungeonPlayerState.h"

#include <stdexcept>

namespace dnf
{
namespace
{
MovePlayerResult ToMovePlayerResult(PositionCheckResult result)
{
    switch (result)
    {
    case PositionCheckResult::Valid:
        return MovePlayerResult::Success;
    case PositionCheckResult::OutsideRoom:
        return MovePlayerResult::OutsideRoom;
    case PositionCheckResult::BlockedByObstacle:
        return MovePlayerResult::BlockedByObstacle;
    }

    throw std::runtime_error("Unknown position check result");
}
} // namespace

DungeonPlayerState::DungeonPlayerState(
    SessionId sessionId,
    RoomId roomId,
    Position position,
    std::uint32_t maxMp,
    std::uint32_t maxHp)
    : sessionId_(sessionId),
      roomId_(roomId),
      position_(position),
      currentHp_(maxHp),
      maxHp_(maxHp),
      currentMp_(maxMp),
      maxMp_(maxMp)
{
    if (sessionId_ == 0 || roomId_ == 0 || position_.z < 0.0f ||
        maxMp_ == 0 || maxHp_ == 0)
    {
        throw std::invalid_argument("Invalid dungeon player state");
    }
}

SessionId DungeonPlayerState::Session() const
{
    return sessionId_;
}

RoomId DungeonPlayerState::CurrentRoom() const
{
    std::lock_guard lock(mutex_);
    return roomId_;
}

Position DungeonPlayerState::CurrentPosition() const
{
    std::lock_guard lock(mutex_);
    return position_;
}

DungeonPlayerSnapshot DungeonPlayerState::Snapshot() const
{
    std::lock_guard lock(mutex_);
    return {
        roomId_,
        position_,
        currentHp_,
        maxHp_,
        currentHp_ > 0,
        currentMp_,
        maxMp_,
        {actionSkillId_, actionPhase_, actionRemainingTicks_}};
}

std::uint32_t DungeonPlayerState::CurrentHp() const
{
    std::lock_guard lock(mutex_);
    return currentHp_;
}

bool DungeonPlayerState::IsAlive() const
{
    std::lock_guard lock(mutex_);
    return currentHp_ > 0;
}

bool DungeonPlayerState::ApplyDamage(std::uint32_t damage)
{
    std::lock_guard lock(mutex_);

    if (damage == 0 || currentHp_ == 0)
    {
        return false;
    }

    currentHp_ = damage >= currentHp_ ? 0 : currentHp_ - damage;
    if (currentHp_ == 0)
    {
        actionSkillId_ = 0;
        actionPhase_ = SkillActionPhase::Idle;
        actionRemainingTicks_ = 0;
        actionActiveTicks_ = 0;
        actionRecoveryTicks_ = 0;
    }

    return true;
}

std::uint32_t DungeonPlayerState::CurrentMp() const
{
    std::lock_guard lock(mutex_);
    return currentMp_;
}

std::uint32_t DungeonPlayerState::RemainingCooldown(SkillId skillId) const
{
    std::lock_guard lock(mutex_);

    const auto cooldownIt = cooldowns_.find(skillId);
    if (cooldownIt == cooldowns_.end())
    {
        return 0;
    }

    return cooldownIt->second;
}

SkillActionSnapshot DungeonPlayerState::CurrentSkillAction() const
{
    std::lock_guard lock(mutex_);
    return {actionSkillId_, actionPhase_, actionRemainingTicks_};
}

BeginSkillResult DungeonPlayerState::BeginSkill(
    SkillId skillId,
    std::uint32_t manaCost,
    std::uint32_t cooldownTicks,
    std::uint32_t startupTicks,
    std::uint32_t activeTicks,
    std::uint32_t recoveryTicks)
{
    std::lock_guard lock(mutex_);

    if (currentHp_ == 0)
    {
        return BeginSkillResult::Dead;
    }

    if (skillId == 0 || activeTicks == 0)
    {
        return BeginSkillResult::InvalidSkill;
    }

    if (cooldowns_.contains(skillId))
    {
        return BeginSkillResult::OnCooldown;
    }

    if (actionPhase_ != SkillActionPhase::Idle)
    {
        return BeginSkillResult::Busy;
    }

    if (currentMp_ < manaCost)
    {
        return BeginSkillResult::NotEnoughMana;
    }

    currentMp_ -= manaCost;

    if (cooldownTicks > 0)
    {
        cooldowns_.emplace(skillId, cooldownTicks);
    }

    actionSkillId_ = skillId;
    actionActiveTicks_ = activeTicks;
    actionRecoveryTicks_ = recoveryTicks;

    if (startupTicks > 0)
    {
        actionPhase_ = SkillActionPhase::Startup;
        actionRemainingTicks_ = startupTicks;
    }
    else
    {
        actionPhase_ = SkillActionPhase::Active;
        actionRemainingTicks_ = activeTicks;
    }

    return BeginSkillResult::Success;
}

void DungeonPlayerState::AdvanceCombatTick()
{
    std::lock_guard lock(mutex_);

    for (auto cooldownIt = cooldowns_.begin();
         cooldownIt != cooldowns_.end();)
    {
        --cooldownIt->second;

        if (cooldownIt->second == 0)
        {
            cooldownIt = cooldowns_.erase(cooldownIt);
        }
        else
        {
            ++cooldownIt;
        }
    }

    if (actionPhase_ == SkillActionPhase::Idle)
    {
        return;
    }

    --actionRemainingTicks_;
    if (actionRemainingTicks_ > 0)
    {
        return;
    }

    if (actionPhase_ == SkillActionPhase::Startup)
    {
        actionPhase_ = SkillActionPhase::Active;
        actionRemainingTicks_ = actionActiveTicks_;
        return;
    }

    if (actionPhase_ == SkillActionPhase::Active &&
        actionRecoveryTicks_ > 0)
    {
        actionPhase_ = SkillActionPhase::Recovery;
        actionRemainingTicks_ = actionRecoveryTicks_;
        return;
    }

    actionSkillId_ = 0;
    actionPhase_ = SkillActionPhase::Idle;
    actionRemainingTicks_ = 0;
    actionActiveTicks_ = 0;
    actionRecoveryTicks_ = 0;
}

MovePlayerResult DungeonPlayerState::MoveTo(
    const RoomState& room,
    const Position& nextPosition)
{
    std::lock_guard lock(mutex_);

    if (currentHp_ == 0)
    {
        return MovePlayerResult::Dead;
    }

    if (roomId_ != room.Id())
    {
        return MovePlayerResult::WrongRoom;
    }

    const PositionCheckResult checkResult = room.CheckPosition(nextPosition);
    if (checkResult != PositionCheckResult::Valid)
    {
        return ToMovePlayerResult(checkResult);
    }

    position_ = nextPosition;
    return MovePlayerResult::Success;
}

MovePlayerResult DungeonPlayerState::EnterRoom(
    const RoomState& room,
    const Position& spawnPosition)
{
    std::lock_guard lock(mutex_);

    if (currentHp_ == 0)
    {
        return MovePlayerResult::Dead;
    }

    const PositionCheckResult checkResult = room.CheckPosition(spawnPosition);
    if (checkResult != PositionCheckResult::Valid)
    {
        return ToMovePlayerResult(checkResult);
    }

    roomId_ = room.Id();
    position_ = spawnPosition;
    return MovePlayerResult::Success;
}
} // namespace dnf
