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
    std::uint32_t maxMp)
    : sessionId_(sessionId),
      roomId_(roomId),
      position_(position),
      currentMp_(maxMp),
      maxMp_(maxMp)
{
    if (sessionId_ == 0 || roomId_ == 0 || position_.z < 0.0f ||
        maxMp_ == 0)
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
    return {roomId_, position_, currentMp_, maxMp_};
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

BeginSkillResult DungeonPlayerState::BeginSkill(
    SkillId skillId,
    std::uint32_t manaCost,
    std::uint32_t cooldownTicks)
{
    std::lock_guard lock(mutex_);

    if (skillId == 0)
    {
        return BeginSkillResult::InvalidSkill;
    }

    if (cooldowns_.contains(skillId))
    {
        return BeginSkillResult::OnCooldown;
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
}

MovePlayerResult DungeonPlayerState::MoveTo(
    const RoomState& room,
    const Position& nextPosition)
{
    std::lock_guard lock(mutex_);

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
