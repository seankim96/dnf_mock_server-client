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
    Position position)
    : sessionId_(sessionId),
      roomId_(roomId),
      position_(position)
{
    if (sessionId_ == 0 || roomId_ == 0 || position_.z < 0.0f)
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
