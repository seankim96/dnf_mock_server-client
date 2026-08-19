#pragma once

#include "DungeonManager.h"
#include "DungeonUdpManager.h"

#include <cstddef>

namespace dnf
{
constexpr float DUNGEON_PLAYER_MOVE_SPEED = 300.0f;
constexpr float MAX_DUNGEON_DELTA_SECONDS = 0.1f;

struct MovementProcessResult
{
    std::size_t receivedCount = 0;
    std::size_t appliedCount = 0;
    std::size_t rejectedCount = 0;
};

class DungeonMovementProcessor
{
public:
    DungeonMovementProcessor(
        DungeonManager& dungeonManager,
        DungeonUdpManager& udpManager);

    MovementProcessResult Process(
        DungeonId dungeonId,
        float deltaSeconds);

private:
    DungeonManager& dungeonManager_;
    DungeonUdpManager& udpManager_;
};
} // namespace dnf
