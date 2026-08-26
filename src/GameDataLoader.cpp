#include "GameDataLoader.h"

#include "ChannelManager.h"
#include "DungeonCatalog.h"
#include "DungeonRoom.h"
#include "EnemyCatalog.h"
#include "SkillCatalog.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dnf
{
namespace
{
using boost::property_tree::ptree;

template <typename Value>
Value RequireValue(
    const ptree& tree,
    const std::string& field,
    const std::string& context)
{
    try
    {
        return tree.get<Value>(field);
    }
    catch (const boost::property_tree::ptree_error& error)
    {
        throw std::runtime_error(
            context + ": field '" + field + "' is missing or invalid: " +
            error.what());
    }
}

const ptree& RequireChild(
    const ptree& tree,
    const char* field,
    const std::string& context)
{
    try
    {
        const ptree::path_type childPath(field);
        return tree.get_child(childPath);
    }
    catch (const boost::property_tree::ptree_error& error)
    {
        throw std::runtime_error(
            context + ": object or array '" + field +
            "' is missing or invalid: " + error.what());
    }
}

void RequireArray(const ptree& tree, const std::string& context)
{
    for (const auto& [key, unused] : tree)
    {
        static_cast<void>(unused);
        if (!key.empty())
        {
            throw std::runtime_error(context + " must be a JSON array");
        }
    }
}

float RequireFiniteFloat(
    const ptree& tree,
    const std::string& field,
    const std::string& context)
{
    const float value = RequireValue<float>(tree, field, context);
    if (!std::isfinite(value))
    {
        throw std::runtime_error(
            context + ": field '" + field + "' must be finite");
    }

    return value;
}

ptree ReadJsonFile(const std::filesystem::path& path)
{
    if (!std::filesystem::is_regular_file(path))
    {
        throw std::runtime_error(
            "Game data file does not exist: " + path.string());
    }

    ptree tree;
    try
    {
        boost::property_tree::read_json(path.string(), tree);
    }
    catch (const boost::property_tree::json_parser::json_parser_error& error)
    {
        throw std::runtime_error(
            "Failed to parse game data file '" + path.string() +
            "': line " + std::to_string(error.line()) + ": " +
            error.message());
    }

    return tree;
}

std::uint32_t ReadContentVersion(
    const ptree& tree,
    const std::filesystem::path& path)
{
    const std::string context = path.string();
    const std::uint32_t version =
        RequireValue<std::uint32_t>(tree, "contentVersion", context);

    if (version == 0)
    {
        throw std::runtime_error(
            context + ": contentVersion must be greater than zero");
    }

    return version;
}

void AcceptContentVersion(
    std::uint32_t candidate,
    const std::filesystem::path& path,
    GameDataLoadResult& result)
{
    if (result.contentVersion == 0)
    {
        result.contentVersion = candidate;
        return;
    }

    if (candidate != result.contentVersion)
    {
        throw std::runtime_error(
            path.string() + ": contentVersion " +
            std::to_string(candidate) +
            " does not match expected version " +
            std::to_string(result.contentVersion));
    }
}

Position ReadPosition(const ptree& tree, const std::string& context)
{
    Position position;
    position.x = RequireFiniteFloat(tree, "x", context);
    position.y = RequireFiniteFloat(tree, "y", context);
    position.z = RequireFiniteFloat(tree, "z", context);
    return position;
}

CollisionBox ReadCollisionBox(
    const ptree& tree,
    const std::string& context)
{
    CollisionBox collision;
    collision.minimum = ReadPosition(
        RequireChild(tree, "minimum", context),
        context + ".minimum");
    collision.maximum = ReadPosition(
        RequireChild(tree, "maximum", context),
        context + ".maximum");
    return collision;
}

EnemyAiType ParseEnemyAiType(
    const std::string& value,
    const std::string& context)
{
    if (value == "melee")
    {
        return EnemyAiType::Melee;
    }
    if (value == "ranged")
    {
        return EnemyAiType::Ranged;
    }
    if (value == "boss")
    {
        return EnemyAiType::Boss;
    }

    throw std::runtime_error(
        context + ": unknown aiType '" + value +
        "' (expected melee, ranged, or boss)");
}

SkillEffectType ParseSkillEffectType(
    const std::string& value,
    const std::string& context)
{
    if (value == "damage")
    {
        return SkillEffectType::Damage;
    }
    if (value == "heal")
    {
        return SkillEffectType::Heal;
    }
    if (value == "buff")
    {
        return SkillEffectType::Buff;
    }
    if (value == "debuff")
    {
        return SkillEffectType::Debuff;
    }

    throw std::runtime_error(
        context + ": unknown effect type '" + value +
        "' (expected damage, heal, buff, or debuff)");
}

SkillTargetType ParseSkillTargetType(
    const std::string& value,
    const std::string& context)
{
    if (value == "self")
    {
        return SkillTargetType::Self;
    }
    if (value == "party")
    {
        return SkillTargetType::Party;
    }
    if (value == "enemy")
    {
        return SkillTargetType::Enemy;
    }

    throw std::runtime_error(
        context + ": unknown target '" + value +
        "' (expected self, party, or enemy)");
}

SkillStat ParseSkillStat(
    const std::string& value,
    const std::string& context)
{
    if (value == "none")
    {
        return SkillStat::None;
    }
    if (value == "attack")
    {
        return SkillStat::Attack;
    }
    if (value == "defense")
    {
        return SkillStat::Defense;
    }
    if (value == "moveSpeed")
    {
        return SkillStat::MoveSpeed;
    }

    throw std::runtime_error(
        context + ": unknown stat '" + value +
        "' (expected none, attack, defense, or moveSpeed)");
}

std::string ItemContext(
    const std::filesystem::path& path,
    const std::string& collection,
    std::size_t index)
{
    return path.string() + ": " + collection + "[" +
           std::to_string(index) + "]";
}

void LoadChannels(
    const std::filesystem::path& path,
    ChannelManager& channelManager,
    GameDataLoadResult& result)
{
    const ptree tree = ReadJsonFile(path);
    AcceptContentVersion(ReadContentVersion(tree, path), path, result);

    const std::string pathContext = path.string();
    const ptree& channels = RequireChild(tree, "channels", pathContext);
    RequireArray(channels, pathContext + ": channels");

    std::size_t index = 0;
    for (const auto& [unused, node] : channels)
    {
        static_cast<void>(unused);
        const std::string context = ItemContext(path, "channels", index);

        const ChannelId id = RequireValue<ChannelId>(node, "id", context);
        const std::string name =
            RequireValue<std::string>(node, "name", context);
        const std::size_t maxPlayers =
            RequireValue<std::size_t>(node, "maxPlayers", context);

        if (!channelManager.AddChannel(id, name, maxPlayers))
        {
            throw std::runtime_error(
                context + ": channel id " + std::to_string(id) +
                " failed catalog validation (invalid fields or duplicate ID)");
        }

        ++result.channelCount;
        ++index;
    }

    if (index == 0)
    {
        throw std::runtime_error(path.string() + ": channels cannot be empty");
    }
}

SkillEffect ReadSkillEffect(
    const ptree& tree,
    const std::string& context)
{
    SkillEffect effect;
    effect.type = ParseSkillEffectType(
        RequireValue<std::string>(tree, "type", context),
        context);
    effect.target = ParseSkillTargetType(
        RequireValue<std::string>(tree, "target", context),
        context);
    effect.stat = ParseSkillStat(
        RequireValue<std::string>(tree, "stat", context),
        context);
    effect.value = RequireFiniteFloat(tree, "value", context);
    effect.durationTicks =
        RequireValue<std::uint32_t>(tree, "durationTicks", context);
    return effect;
}

void LoadSkills(
    const std::filesystem::path& path,
    SkillCatalog& skillCatalog,
    GameDataLoadResult& result)
{
    const ptree tree = ReadJsonFile(path);
    AcceptContentVersion(ReadContentVersion(tree, path), path, result);

    const std::string pathContext = path.string();
    const ptree& skills = RequireChild(tree, "skills", pathContext);
    RequireArray(skills, pathContext + ": skills");

    std::size_t index = 0;
    for (const auto& [unused, node] : skills)
    {
        static_cast<void>(unused);
        const std::string context = ItemContext(path, "skills", index);

        SkillTemplate skill;
        skill.id = RequireValue<SkillId>(node, "id", context);
        skill.name = RequireValue<std::string>(node, "name", context);
        skill.cooldownTicks =
            RequireValue<std::uint32_t>(node, "cooldownTicks", context);
        skill.manaCost =
            RequireValue<std::uint32_t>(node, "manaCost", context);
        skill.startupTicks =
            RequireValue<std::uint32_t>(node, "startupTicks", context);
        skill.activeTicks =
            RequireValue<std::uint32_t>(node, "activeTicks", context);
        skill.recoveryTicks =
            RequireValue<std::uint32_t>(node, "recoveryTicks", context);

        const ptree& hitBox = RequireChild(node, "hitBox", context);
        skill.hitBox.forwardRange =
            RequireFiniteFloat(hitBox, "forwardRange", context + ".hitBox");
        skill.hitBox.backwardRange =
            RequireFiniteFloat(hitBox, "backwardRange", context + ".hitBox");
        skill.hitBox.halfDepth =
            RequireFiniteFloat(hitBox, "halfDepth", context + ".hitBox");
        skill.hitBox.height =
            RequireFiniteFloat(hitBox, "height", context + ".hitBox");

        const ptree& effects = RequireChild(node, "effects", context);
        RequireArray(effects, context + ".effects");

        std::size_t effectIndex = 0;
        for (const auto& [effectKey, effectNode] : effects)
        {
            static_cast<void>(effectKey);
            skill.effects.push_back(ReadSkillEffect(
                effectNode,
                context + ".effects[" + std::to_string(effectIndex) + "]"));
            ++effectIndex;
        }

        const SkillId id = skill.id;
        if (!skillCatalog.AddSkill(std::move(skill)))
        {
            throw std::runtime_error(
                context + ": skill id " + std::to_string(id) +
                " failed catalog validation (invalid fields or duplicate ID)");
        }

        ++result.skillCount;
        ++index;
    }

    if (index == 0)
    {
        throw std::runtime_error(path.string() + ": skills cannot be empty");
    }
}

void LoadEnemies(
    const std::filesystem::path& path,
    EnemyCatalog& enemyCatalog,
    GameDataLoadResult& result)
{
    const ptree tree = ReadJsonFile(path);
    AcceptContentVersion(ReadContentVersion(tree, path), path, result);

    const std::string pathContext = path.string();
    const ptree& enemies = RequireChild(tree, "enemies", pathContext);
    RequireArray(enemies, pathContext + ": enemies");

    std::size_t index = 0;
    for (const auto& [unused, node] : enemies)
    {
        static_cast<void>(unused);
        const std::string context = ItemContext(path, "enemies", index);

        EnemyTemplate enemy;
        enemy.id = RequireValue<EnemyTemplateId>(node, "id", context);
        enemy.name = RequireValue<std::string>(node, "name", context);
        enemy.maxHp =
            RequireValue<std::uint32_t>(node, "maxHp", context);
        enemy.moveSpeed = RequireFiniteFloat(node, "moveSpeed", context);
        enemy.aiType = ParseEnemyAiType(
            RequireValue<std::string>(node, "aiType", context),
            context);
        enemy.collision = ReadCollisionBox(
            RequireChild(node, "collision", context),
            context + ".collision");

        const EnemyTemplateId id = enemy.id;
        if (!enemyCatalog.AddEnemy(std::move(enemy)))
        {
            throw std::runtime_error(
                context + ": enemy id " + std::to_string(id) +
                " failed catalog validation (invalid fields or duplicate ID)");
        }

        ++result.enemyCount;
        ++index;
    }

    if (index == 0)
    {
        throw std::runtime_error(path.string() + ": enemies cannot be empty");
    }
}

PortalTemplate ReadPortal(
    const ptree& tree,
    const std::string& context)
{
    PortalTemplate portal;
    portal.id = RequireValue<PortalId>(tree, "id", context);
    portal.triggerArea = ReadCollisionBox(
        RequireChild(tree, "triggerArea", context),
        context + ".triggerArea");
    portal.targetRoomId =
        RequireValue<RoomId>(tree, "targetRoomId", context);
    portal.targetPosition = ReadPosition(
        RequireChild(tree, "targetPosition", context),
        context + ".targetPosition");
    portal.requiresRoomClear =
        RequireValue<bool>(tree, "requiresRoomClear", context);
    return portal;
}

ObstacleTemplate ReadObstacle(
    const ptree& tree,
    const std::string& context)
{
    ObstacleTemplate obstacle;
    obstacle.id = RequireValue<ObstacleId>(tree, "id", context);
    obstacle.collision = ReadCollisionBox(
        RequireChild(tree, "collision", context),
        context + ".collision");
    obstacle.destructible =
        RequireValue<bool>(tree, "destructible", context);
    obstacle.maxHp = RequireValue<std::uint32_t>(tree, "maxHp", context);
    return obstacle;
}

EnemySpawnTemplate ReadEnemySpawn(
    const ptree& tree,
    const std::string& context)
{
    EnemySpawnTemplate spawn;
    spawn.id = RequireValue<EnemySpawnId>(tree, "id", context);
    spawn.enemyTemplateId =
        RequireValue<EnemyTemplateId>(tree, "enemyTemplateId", context);
    spawn.position = ReadPosition(
        RequireChild(tree, "position", context),
        context + ".position");
    spawn.wave = RequireValue<std::uint32_t>(tree, "wave", context);
    return spawn;
}

RoomTemplate ReadRoom(
    const ptree& tree,
    const std::string& context)
{
    RoomTemplate room;
    room.id = RequireValue<RoomId>(tree, "id", context);
    room.width = RequireFiniteFloat(tree, "width", context);
    room.depth = RequireFiniteFloat(tree, "depth", context);
    room.playerSpawn = ReadPosition(
        RequireChild(tree, "playerSpawn", context),
        context + ".playerSpawn");

    const ptree& portals = RequireChild(tree, "portals", context);
    RequireArray(portals, context + ".portals");
    std::size_t portalIndex = 0;
    for (const auto& [unused, portalNode] : portals)
    {
        static_cast<void>(unused);
        room.portals.push_back(ReadPortal(
            portalNode,
            context + ".portals[" + std::to_string(portalIndex) + "]"));
        ++portalIndex;
    }

    const ptree& obstacles = RequireChild(tree, "obstacles", context);
    RequireArray(obstacles, context + ".obstacles");
    std::size_t obstacleIndex = 0;
    for (const auto& [unused, obstacleNode] : obstacles)
    {
        static_cast<void>(unused);
        room.obstacles.push_back(ReadObstacle(
            obstacleNode,
            context + ".obstacles[" +
                std::to_string(obstacleIndex) + "]"));
        ++obstacleIndex;
    }

    const ptree& enemySpawns = RequireChild(tree, "enemySpawns", context);
    RequireArray(enemySpawns, context + ".enemySpawns");
    std::size_t enemySpawnIndex = 0;
    for (const auto& [unused, enemySpawnNode] : enemySpawns)
    {
        static_cast<void>(unused);
        room.enemySpawns.push_back(ReadEnemySpawn(
            enemySpawnNode,
            context + ".enemySpawns[" +
                std::to_string(enemySpawnIndex) + "]"));
        ++enemySpawnIndex;
    }

    return room;
}

void ValidateDungeonReferences(
    const DungeonTemplate& dungeon,
    const EnemyCatalog& enemyCatalog,
    const std::filesystem::path& path)
{
    std::unordered_set<RoomId> roomIds;
    for (const RoomTemplate& room : dungeon.rooms)
    {
        roomIds.insert(room.id);
    }

    for (const RoomTemplate& room : dungeon.rooms)
    {
        for (const PortalTemplate& portal : room.portals)
        {
            if (!roomIds.contains(portal.targetRoomId))
            {
                throw std::runtime_error(
                    path.string() + ": dungeon id " +
                    std::to_string(dungeon.id) + ", room id " +
                    std::to_string(room.id) + ", portal id " +
                    std::to_string(portal.id) +
                    " references missing target room id " +
                    std::to_string(portal.targetRoomId));
            }
        }

        for (const EnemySpawnTemplate& spawn : room.enemySpawns)
        {
            if (!enemyCatalog.GetEnemy(spawn.enemyTemplateId).has_value())
            {
                throw std::runtime_error(
                    path.string() + ": dungeon id " +
                    std::to_string(dungeon.id) + ", room id " +
                    std::to_string(room.id) + ", enemy spawn id " +
                    std::to_string(spawn.id) +
                    " references missing enemy id " +
                    std::to_string(spawn.enemyTemplateId));
            }
        }
    }
}

void LoadDungeon(
    const std::filesystem::path& path,
    EnemyCatalog& enemyCatalog,
    DungeonCatalog& dungeonCatalog,
    GameDataLoadResult& result)
{
    const ptree tree = ReadJsonFile(path);
    AcceptContentVersion(ReadContentVersion(tree, path), path, result);

    const std::string pathContext = path.string();
    const ptree& dungeonNode =
        RequireChild(tree, "dungeon", pathContext);
    const std::string context = pathContext + ": dungeon";

    DungeonTemplate dungeon;
    dungeon.id =
        RequireValue<DungeonTemplateId>(dungeonNode, "id", context);
    dungeon.name = RequireValue<std::string>(dungeonNode, "name", context);
    dungeon.recommendedPartySize = RequireValue<std::uint8_t>(
        dungeonNode,
        "recommendedPartySize",
        context);
    dungeon.maxPartySize = RequireValue<std::uint8_t>(
        dungeonNode,
        "maxPartySize",
        context);

    const ptree& rooms = RequireChild(dungeonNode, "rooms", context);
    RequireArray(rooms, context + ".rooms");
    std::size_t roomIndex = 0;
    for (const auto& [unused, roomNode] : rooms)
    {
        static_cast<void>(unused);
        dungeon.rooms.push_back(ReadRoom(
            roomNode,
            context + ".rooms[" + std::to_string(roomIndex) + "]"));
        ++roomIndex;
    }

    ValidateDungeonReferences(dungeon, enemyCatalog, path);

    const DungeonTemplateId id = dungeon.id;
    const std::string name = dungeon.name;
    const std::uint8_t recommendedPartySize = dungeon.recommendedPartySize;
    const std::uint8_t maxPartySize = dungeon.maxPartySize;

    if (!dungeonCatalog.AddDungeon(
            id,
            name,
            std::move(dungeon.rooms),
            recommendedPartySize,
            maxPartySize))
    {
        throw std::runtime_error(
            context + " id " + std::to_string(id) +
            " failed catalog validation (invalid fields, invalid references, "
            "or duplicate ID)");
    }

    ++result.dungeonCount;
}

std::vector<std::filesystem::path> FindDungeonFiles(
    const std::filesystem::path& directory)
{
    if (!std::filesystem::is_directory(directory))
    {
        throw std::runtime_error(
            "Game data dungeon directory does not exist: " +
            directory.string());
    }

    std::vector<std::filesystem::path> paths;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            paths.push_back(entry.path());
        }
    }

    std::sort(paths.begin(), paths.end());
    if (paths.empty())
    {
        throw std::runtime_error(
            "No dungeon JSON files found in: " + directory.string());
    }

    return paths;
}
} // namespace

GameDataLoader::GameDataLoader(
    ChannelManager& channelManager,
    SkillCatalog& skillCatalog,
    EnemyCatalog& enemyCatalog,
    DungeonCatalog& dungeonCatalog)
    : channelManager_(channelManager),
      skillCatalog_(skillCatalog),
      enemyCatalog_(enemyCatalog),
      dungeonCatalog_(dungeonCatalog)
{
}

GameDataLoadResult GameDataLoader::Load(
    const std::filesystem::path& dataDirectory) const
{
    if (!std::filesystem::is_directory(dataDirectory))
    {
        throw std::runtime_error(
            "Game data directory does not exist: " + dataDirectory.string());
    }

    GameDataLoadResult result;

    LoadChannels(
        dataDirectory / "channels.json",
        channelManager_,
        result);
    LoadSkills(dataDirectory / "skills.json", skillCatalog_, result);
    LoadEnemies(dataDirectory / "enemies.json", enemyCatalog_, result);

    for (const std::filesystem::path& dungeonPath :
         FindDungeonFiles(dataDirectory / "dungeons"))
    {
        LoadDungeon(
            dungeonPath,
            enemyCatalog_,
            dungeonCatalog_,
            result);
    }

    return result;
}
} // namespace dnf
