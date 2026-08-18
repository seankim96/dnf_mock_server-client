#include "DungeonCatalog.h"

#include <utility>

namespace dnf
{
bool DungeonCatalog::AddDungeon(
    DungeonTemplateId templateId,
    std::string name,
    std::uint32_t roomCount)
{
    std::lock_guard lock(mutex_);

    if (templateId == 0 || name.empty() || roomCount == 0 ||
        dungeons_.contains(templateId))
    {
        return false;
    }

    DungeonTemplate dungeon;
    dungeon.id = templateId;
    dungeon.name = std::move(name);
    dungeon.roomCount = roomCount;

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
