#pragma once

#include "DungeonRoom.h"
#include "RoomState.h"
#include "SessionId.h"

#include <mutex>

namespace dnf
{
enum class MovePlayerResult
{
    Success,
    WrongRoom,
    OutsideRoom,
    BlockedByObstacle
};

class DungeonPlayerState
{
public:
    DungeonPlayerState(
        SessionId sessionId,
        RoomId roomId,
        Position position);

    SessionId Session() const;
    RoomId CurrentRoom() const;
    Position CurrentPosition() const;

    MovePlayerResult MoveTo(
        const RoomState& room,
        const Position& nextPosition);

    MovePlayerResult EnterRoom(
        const RoomState& room,
        const Position& spawnPosition);

private:
    SessionId sessionId_;

    mutable std::mutex mutex_;
    RoomId roomId_;
    Position position_;
};
} // namespace dnf
