#include "DungeonRoom.h"

namespace dnf
{
bool IsInsideRoom(const RoomTemplate& room, const Position& position)
{
    return position.x >= 0.0f && position.x <= room.width &&
           position.y >= 0.0f && position.y <= room.depth;
}
} // namespace dnf
