#include "DungeonInputProcessor.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace dnf
{
DungeonInputProcessor::DungeonInputProcessor(
    DungeonManager& dungeonManager,
    DungeonUdpManager& udpManager)
    : dungeonManager_(dungeonManager),
      udpManager_(udpManager)
{
}

InputProcessResult DungeonInputProcessor::Process(
    DungeonId dungeonId,
    float deltaSeconds)
{
    InputProcessResult result;

    const auto dungeon = dungeonManager_.FindDungeon(dungeonId);
    if (dungeon == nullptr ||
        dungeon->State() != DungeonState::Running ||
        !std::isfinite(deltaSeconds) ||
        deltaSeconds <= 0.0f)
    {
        return result;
    }

    std::unordered_map<SessionId, PlayerInputMessage> latestInputs;

    AuthenticatedPlayerInput queuedInput;
    while (udpManager_.TryPopInput(dungeonId, queuedInput))
    {
        latestInputs[queuedInput.sessionId] = queuedInput.input;
        ++result.receivedCount;
    }

    const float safeDeltaSeconds =
        std::min(deltaSeconds, MAX_DUNGEON_DELTA_SECONDS);

    for (const auto& [sessionId, input] : latestInputs)
    {
        const auto player = dungeon->FindPlayer(sessionId);
        if (player == nullptr)
        {
            ++result.rejectedCount;
            continue;
        }

        const DungeonPlayerSnapshot current = player->Snapshot();
        const auto room = dungeon->FindRoom(current.roomId);
        if (room == nullptr)
        {
            ++result.rejectedCount;
            continue;
        }

        float moveX = input.moveX;
        float moveY = input.moveY;
        const float moveLength = std::sqrt(moveX * moveX + moveY * moveY);

        if (moveLength > 1.0f)
        {
            moveX /= moveLength;
            moveY /= moveLength;
        }

        Position nextPosition = current.position;
        nextPosition.x +=
            moveX * DUNGEON_PLAYER_MOVE_SPEED * safeDeltaSeconds;
        nextPosition.y +=
            moveY * DUNGEON_PLAYER_MOVE_SPEED * safeDeltaSeconds;

        if (player->MoveTo(*room, nextPosition) == MovePlayerResult::Success)
        {
            ++result.appliedCount;
        }
        else
        {
            ++result.rejectedCount;
        }
    }

    return result;
}
} // namespace dnf
