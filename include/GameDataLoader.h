#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace dnf
{
class ChannelManager;
class DungeonCatalog;
class EnemyCatalog;
class SkillCatalog;

struct GameDataLoadResult
{
    std::uint32_t contentVersion = 0;
    std::size_t channelCount = 0;
    std::size_t skillCount = 0;
    std::size_t enemyCount = 0;
    std::size_t dungeonCount = 0;
};

class GameDataLoader
{
public:
    GameDataLoader(
        ChannelManager& channelManager,
        SkillCatalog& skillCatalog,
        EnemyCatalog& enemyCatalog,
        DungeonCatalog& dungeonCatalog);

    GameDataLoadResult Load(
        const std::filesystem::path& dataDirectory) const;

private:
    ChannelManager& channelManager_;
    SkillCatalog& skillCatalog_;
    EnemyCatalog& enemyCatalog_;
    DungeonCatalog& dungeonCatalog_;
};
} // namespace dnf
