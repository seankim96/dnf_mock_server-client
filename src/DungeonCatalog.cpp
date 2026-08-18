#include "DungeonCatalog.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace dnf
{
bool DungeonCatalog::AddDungeon(
    DungeonTemplateId templateId,
    std::string name,
    std::vector<RoomTemplate> rooms)
{
    std::lock_guard lock(mutex_);

    if (templateId == 0 || name.empty() || rooms.empty() ||
        dungeons_.contains(templateId))
    {
        return false;
    }

    std::unordered_set<RoomId> roomIds;

    for (const RoomTemplate& room : rooms)
    {
        if (room.id == 0 || room.width <= 0.0f || room.depth <= 0.0f ||
            room.playerSpawn.z < 0.0f ||
            !IsInsideRoom(room, room.playerSpawn) ||
            !roomIds.insert(room.id).second)
        {
            return false;
        }
    }

    for (const RoomTemplate& room : rooms)
    {
        std::unordered_set<PortalId> portalIds;

        for (const PortalTemplate& portal : room.portals)
        {
            if (portal.id == 0 ||
                !portalIds.insert(portal.id).second ||
                !IsValidCollisionBox(portal.triggerArea) ||
                portal.triggerArea.minimum.z < 0.0f ||
                !IsInsideRoom(room, portal.triggerArea.minimum) ||
                !IsInsideRoom(room, portal.triggerArea.maximum) ||
                !roomIds.contains(portal.targetRoomId))
            {
                return false;
            }

            const auto targetRoomIt = std::find_if(
                rooms.begin(),
                rooms.end(),
                [&portal](const RoomTemplate& targetRoom)
                {
                    return targetRoom.id == portal.targetRoomId;
                });

            if (portal.targetPosition.z < 0.0f ||
                !IsInsideRoom(*targetRoomIt, portal.targetPosition))
            {
                return false;
            }
        }
    }

    DungeonTemplate dungeon;
    dungeon.id = templateId;
    dungeon.name = std::move(name);
    dungeon.rooms = std::move(rooms);

    dungeons_.emplace(templateId, std::move(dungeon));
    return true;
}

std::optional<DungeonTemplate> DungeonCatalog::GetDungeon(
    DungeonTemplateId templateId) const
{
    std::lock_guard lock(mutex_);

    auto dungeonIt = dungeons_.find(templateId);
    if (dungeonIt == dungeons_.end())
    {
        return std::nullopt;
    }

    return dungeonIt->second;
}
} // namespace dnf
