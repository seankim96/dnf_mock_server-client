#pragma once

#include "DungeonRoom.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dnf
{
using DungeonTemplateId = std::uint32_t;

class EnemyCatalog;

struct DungeonTemplate
{
    DungeonTemplateId id = 0;
    std::string name;
    std::vector<RoomTemplate> rooms;
};

class DungeonCatalog
{
public:
    explicit DungeonCatalog(const EnemyCatalog& enemyCatalog);

    bool AddDungeon(
        DungeonTemplateId templateId,
        std::string name,
        std::vector<RoomTemplate> rooms);

    std::optional<DungeonTemplate> GetDungeon(
        DungeonTemplateId templateId) const;

private:
    const EnemyCatalog& enemyCatalog_;

    mutable std::mutex mutex_;
    std::unordered_map<DungeonTemplateId, DungeonTemplate> dungeons_;
};
} // namespace dnf
