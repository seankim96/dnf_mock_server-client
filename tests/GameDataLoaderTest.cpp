#include "ChannelManager.h"
#include "DungeonCatalog.h"
#include "EnemyCatalog.h"
#include "GameDataLoader.h"
#include "SkillCatalog.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
std::filesystem::path FindProjectRoot()
{
    std::filesystem::path candidate = std::filesystem::current_path();

    for (int level = 0; level < 6; ++level)
    {
        if (std::filesystem::is_regular_file(
                candidate / "data" / "channels.json"))
        {
            return candidate;
        }

        if (!candidate.has_parent_path())
        {
            break;
        }
        candidate = candidate.parent_path();
    }

    const std::filesystem::path sourcePath =
        std::filesystem::absolute(std::filesystem::path(__FILE__));
    candidate = sourcePath.parent_path().parent_path();
    if (std::filesystem::is_regular_file(
            candidate / "data" / "channels.json"))
    {
        return candidate;
    }

    throw std::runtime_error("Could not locate project data directory");
}

void TestLoadsProjectGameData()
{
    dnf::ChannelManager channelManager;
    dnf::SkillCatalog skillCatalog;
    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);
    dnf::GameDataLoader loader(
        channelManager,
        skillCatalog,
        enemyCatalog,
        dungeonCatalog);

    const dnf::GameDataLoadResult result =
        loader.Load(FindProjectRoot() / "data");

    assert(result.contentVersion == 1);
    assert(result.channelCount == 3);
    assert(result.skillCount == 1);
    assert(result.enemyCount == 1);
    assert(result.dungeonCount == 1);

    const auto channels = channelManager.GetChannelList();
    assert(channels.size() == 3);
    assert(channels[0].name == "Channel 1");

    const auto skill = skillCatalog.GetSkill(1001);
    assert(skill.has_value());
    assert(skill->effects.size() == 2);

    const auto enemy = enemyCatalog.GetEnemy(2001);
    assert(enemy.has_value());
    assert(enemy->aiType == dnf::EnemyAiType::Melee);

    const auto dungeon = dungeonCatalog.GetDungeon(1001);
    assert(dungeon.has_value());
    assert(dungeon->rooms.size() == 2);
    assert(dungeon->rooms[0].portals[0].targetRoomId == 2);
    assert(dungeon->rooms[0].enemySpawns[0].enemyTemplateId == 2001);
}

void TestRejectsInvalidReferenceWithContext()
{
    dnf::ChannelManager channelManager;
    dnf::SkillCatalog skillCatalog;
    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);
    dnf::GameDataLoader loader(
        channelManager,
        skillCatalog,
        enemyCatalog,
        dungeonCatalog);

    bool rejected = false;
    try
    {
        loader.Load(
            FindProjectRoot() / "tests" / "data" /
            "invalid_game_data");
    }
    catch (const std::runtime_error& error)
    {
        const std::string message = error.what();
        assert(message.find("broken_forest.json") != std::string::npos);
        assert(message.find("portal id 1") != std::string::npos);
        assert(message.find("target room id 999") != std::string::npos);
        rejected = true;
    }

    assert(rejected);
}

void TestRejectsMissingDirectory()
{
    dnf::ChannelManager channelManager;
    dnf::SkillCatalog skillCatalog;
    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);
    dnf::GameDataLoader loader(
        channelManager,
        skillCatalog,
        enemyCatalog,
        dungeonCatalog);

    bool rejected = false;
    try
    {
        loader.Load(FindProjectRoot() / "tests" / "data" / "missing");
    }
    catch (const std::runtime_error& error)
    {
        assert(
            std::string(error.what()).find("directory does not exist") !=
            std::string::npos);
        rejected = true;
    }

    assert(rejected);
}
} // namespace

int main()
{
    TestLoadsProjectGameData();
    TestRejectsInvalidReferenceWithContext();
    TestRejectsMissingDirectory();

    std::cout << "All game data loader tests passed.\n";
    return 0;
}
