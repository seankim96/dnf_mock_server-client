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

struct DungeonTemplate
{
    DungeonTemplateId id = 0;
    std::string name;
    std::vector<RoomTemplate> rooms;
};

class DungeonCatalog
{
public:
    bool AddDungeon(
        DungeonTemplateId templateId,
        std::string name,
        std::vector<RoomTemplate> rooms);

    std::optional<DungeonTemplate> GetDungeon(
        DungeonTemplateId templateId) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<DungeonTemplateId, DungeonTemplate> dungeons_;
};
} // namespace dnf
