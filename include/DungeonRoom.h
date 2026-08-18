#pragma once

#include <cstdint>
#include <vector>

namespace dnf
{
using RoomId = std::uint32_t;

struct Position
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct CollisionBox
{
    Position minimum;
    Position maximum;
};

using PortalId = std::uint32_t;

struct PortalTemplate
{
    PortalId id = 0;
    CollisionBox triggerArea;

    RoomId targetRoomId = 0;
    Position targetPosition;

    bool requiresRoomClear = true;
};

using ObstacleId = std::uint32_t;

struct ObstacleTemplate
{
    ObstacleId id = 0;
    CollisionBox collision;

    bool destructible = false;
    std::uint32_t maxHp = 0;
};

struct RoomTemplate
{
    RoomId id = 0;

    // X축으로 이동할 수 있는 길이
    float width = 0.0f;

    // Y축으로 이동할 수 있는 깊이
    float depth = 0.0f;

    Position playerSpawn;
    std::vector<PortalTemplate> portals;
    std::vector<ObstacleTemplate> obstacles;
};

bool IsInsideRoom(const RoomTemplate& room, const Position& position);
bool IsValidCollisionBox(const CollisionBox& box);
bool IsInsideCollisionBox(const CollisionBox& box, const Position& position);
} // namespace dnf
