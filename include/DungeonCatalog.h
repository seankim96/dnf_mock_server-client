#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace dnf
{
using DungeonTemplateId = std::uint32_t;

struct DungeonTemplate
{
    DungeonTemplateId id = 0;
    std::string name;
    std::uint32_t roomCount = 0;
};

class DungeonCatalog
{
public:
    bool AddDungeon(
        DungeonTemplateId templateId,
        std::string name,
        std::uint32_t roomCount);

    std::optional<DungeonTemplate> GetDungeon(
        DungeonTemplateId templateId) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<DungeonTemplateId, DungeonTemplate> dungeons_;
};
} // namespace dnf
