#include "DungeonRoom.h"

namespace dnf
{
bool IsInsideRoom(const RoomTemplate& room, const Position& position)
{
    return position.x >= 0.0f && position.x <= room.width &&
           position.y >= 0.0f && position.y <= room.depth;
}

bool IsValidCollisionBox(const CollisionBox& box)
{
    return box.minimum.x < box.maximum.x &&
           box.minimum.y < box.maximum.y &&
           box.minimum.z < box.maximum.z;
}

bool IsInsideCollisionBox(const CollisionBox& box, const Position& position)
{
    return position.x >= box.minimum.x && position.x <= box.maximum.x &&
           position.y >= box.minimum.y && position.y <= box.maximum.y &&
           position.z >= box.minimum.z && position.z <= box.maximum.z;
}
} // namespace dnf
