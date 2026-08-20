#include "DungeonStaticDataProtocol.h"

#include "TcpFlatBufferCodec.h"

#include <flatbuffers/flatbuffer_builder.h>

#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace dnf
{
namespace
{
namespace tcp = Dnf::Protocol::Tcp;

bool IsValidResult(DungeonStaticDataResult result)
{
    return result >= DungeonStaticDataResult::Success &&
           result <= DungeonStaticDataResult::NotDungeonParticipant;
}

bool IsFinite(const Position& position)
{
    return std::isfinite(position.x) &&
           std::isfinite(position.y) &&
           std::isfinite(position.z);
}

bool IsValidBox(const CollisionBox& box)
{
    return IsFinite(box.minimum) && IsFinite(box.maximum) &&
           IsValidCollisionBox(box);
}

bool IsValidEnemyAiType(EnemyAiType aiType)
{
    return aiType >= EnemyAiType::Melee &&
           aiType <= EnemyAiType::Boss;
}

bool IsValidStaticData(
    DungeonStaticDataResult result,
    DungeonId dungeonId,
    DungeonTemplateId dungeonTemplateId,
    const std::vector<RoomTemplate>& rooms,
    const std::vector<EnemyTemplate>& enemyTemplates)
{
    if (!IsValidResult(result))
    {
        return false;
    }

    if (result != DungeonStaticDataResult::Success)
    {
        return dungeonId == 0 && dungeonTemplateId == 0 &&
               rooms.empty() && enemyTemplates.empty();
    }

    if (dungeonId == 0 || dungeonTemplateId == 0 || rooms.empty())
    {
        return false;
    }

    std::unordered_set<EnemyTemplateId> enemyTemplateIds;
    for (const EnemyTemplate& enemy : enemyTemplates)
    {
        if (enemy.id == 0 || enemy.name.empty() || enemy.maxHp == 0 ||
            !std::isfinite(enemy.moveSpeed) || enemy.moveSpeed < 0.0f ||
            !IsValidEnemyAiType(enemy.aiType) ||
            !IsValidBox(enemy.collision) ||
            enemy.collision.minimum.z < 0.0f ||
            !enemyTemplateIds.insert(enemy.id).second)
        {
            return false;
        }
    }

    std::unordered_map<RoomId, const RoomTemplate*> roomById;
    for (const RoomTemplate& room : rooms)
    {
        if (room.id == 0 || !std::isfinite(room.width) ||
            !std::isfinite(room.depth) || room.width <= 0.0f ||
            room.depth <= 0.0f || !IsFinite(room.playerSpawn) ||
            room.playerSpawn.z < 0.0f ||
            !IsInsideRoom(room, room.playerSpawn) ||
            !roomById.emplace(room.id, &room).second)
        {
            return false;
        }
    }

    for (const RoomTemplate& room : rooms)
    {
        std::unordered_set<PortalId> portalIds;
        for (const PortalTemplate& portal : room.portals)
        {
            const auto targetRoom = roomById.find(portal.targetRoomId);
            if (portal.id == 0 ||
                !portalIds.insert(portal.id).second ||
                !IsValidBox(portal.triggerArea) ||
                portal.triggerArea.minimum.z < 0.0f ||
                !IsInsideRoom(room, portal.triggerArea.minimum) ||
                !IsInsideRoom(room, portal.triggerArea.maximum) ||
                targetRoom == roomById.end() ||
                !IsFinite(portal.targetPosition) ||
                portal.targetPosition.z < 0.0f ||
                !IsInsideRoom(
                    *targetRoom->second,
                    portal.targetPosition))
            {
                return false;
            }
        }

        std::unordered_set<ObstacleId> obstacleIds;
        for (const ObstacleTemplate& obstacle : room.obstacles)
        {
            const bool invalidHp =
                (obstacle.destructible && obstacle.maxHp == 0) ||
                (!obstacle.destructible && obstacle.maxHp != 0);

            if (obstacle.id == 0 ||
                !obstacleIds.insert(obstacle.id).second ||
                !IsValidBox(obstacle.collision) ||
                obstacle.collision.minimum.z < 0.0f ||
                !IsInsideRoom(room, obstacle.collision.minimum) ||
                !IsInsideRoom(room, obstacle.collision.maximum) ||
                invalidHp)
            {
                return false;
            }
        }

        std::unordered_set<EnemySpawnId> enemySpawnIds;
        for (const EnemySpawnTemplate& spawn : room.enemySpawns)
        {
            if (spawn.id == 0 ||
                !enemySpawnIds.insert(spawn.id).second ||
                !enemyTemplateIds.contains(spawn.enemyTemplateId) ||
                !IsFinite(spawn.position) || spawn.position.z < 0.0f ||
                !IsInsideRoom(room, spawn.position) || spawn.wave == 0)
            {
                return false;
            }
        }
    }

    return true;
}

tcp::StaticVec3 ToTcpPosition(const Position& position)
{
    return {position.x, position.y, position.z};
}

tcp::StaticCollisionBox ToTcpCollision(const CollisionBox& collision)
{
    return {
        ToTcpPosition(collision.minimum),
        ToTcpPosition(collision.maximum)};
}

Position FromTcpPosition(const tcp::StaticVec3* position)
{
    if (position == nullptr)
    {
        throw std::runtime_error("Missing static position");
    }

    return {position->x(), position->y(), position->z()};
}

CollisionBox FromTcpCollision(const tcp::StaticCollisionBox* collision)
{
    if (collision == nullptr)
    {
        throw std::runtime_error("Missing static collision box");
    }

    return {
        FromTcpPosition(&collision->minimum()),
        FromTcpPosition(&collision->maximum())};
}
} // namespace

std::vector<std::uint8_t> EncodeDungeonStaticDataRequestPayload(
    DungeonId dungeonId)
{
    if (dungeonId == 0)
    {
        throw std::invalid_argument("Dungeon ID must not be zero");
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto request =
        tcp::CreateDungeonStaticDataRequest(builder, dungeonId);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_DungeonStaticDataRequest,
        request.Union());
}

DungeonId DecodeDungeonStaticDataRequestPayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_DungeonStaticDataRequest);
    const auto* request = message->payload_as_DungeonStaticDataRequest();
    if (request == nullptr || request->dungeon_id() == 0)
    {
        throw std::runtime_error("Invalid dungeon static data request");
    }

    return request->dungeon_id();
}

std::vector<std::uint8_t> EncodeDungeonStaticDataResponsePayload(
    DungeonStaticDataResult result,
    DungeonId dungeonId,
    DungeonTemplateId dungeonTemplateId,
    const std::vector<RoomTemplate>& rooms,
    const std::vector<EnemyTemplate>& enemyTemplates)
{
    if (!IsValidStaticData(
            result,
            dungeonId,
            dungeonTemplateId,
            rooms,
            enemyTemplates))
    {
        throw std::invalid_argument("Invalid dungeon static data response");
    }

    flatbuffers::FlatBufferBuilder builder;
    std::vector<flatbuffers::Offset<tcp::RoomStaticData>> roomOffsets;
    roomOffsets.reserve(rooms.size());

    for (const RoomTemplate& room : rooms)
    {
        std::vector<flatbuffers::Offset<tcp::PortalStaticData>> portals;
        portals.reserve(room.portals.size());
        for (const PortalTemplate& portal : room.portals)
        {
            const auto triggerArea = ToTcpCollision(portal.triggerArea);
            const auto targetPosition = ToTcpPosition(portal.targetPosition);
            portals.push_back(tcp::CreatePortalStaticData(
                builder,
                portal.id,
                &triggerArea,
                portal.targetRoomId,
                &targetPosition,
                portal.requiresRoomClear));
        }

        std::vector<flatbuffers::Offset<tcp::ObstacleStaticData>> obstacles;
        obstacles.reserve(room.obstacles.size());
        for (const ObstacleTemplate& obstacle : room.obstacles)
        {
            const auto collision = ToTcpCollision(obstacle.collision);
            obstacles.push_back(tcp::CreateObstacleStaticData(
                builder,
                obstacle.id,
                &collision,
                obstacle.destructible,
                obstacle.maxHp));
        }

        std::vector<flatbuffers::Offset<tcp::EnemySpawnStaticData>> spawns;
        spawns.reserve(room.enemySpawns.size());
        for (const EnemySpawnTemplate& spawn : room.enemySpawns)
        {
            const auto position = ToTcpPosition(spawn.position);
            spawns.push_back(tcp::CreateEnemySpawnStaticData(
                builder,
                spawn.id,
                spawn.enemyTemplateId,
                &position,
                spawn.wave));
        }

        const auto portalVector = builder.CreateVector(portals);
        const auto obstacleVector = builder.CreateVector(obstacles);
        const auto spawnVector = builder.CreateVector(spawns);
        const auto playerSpawn = ToTcpPosition(room.playerSpawn);
        roomOffsets.push_back(tcp::CreateRoomStaticData(
            builder,
            room.id,
            room.width,
            room.depth,
            &playerSpawn,
            portalVector,
            obstacleVector,
            spawnVector));
    }

    std::vector<flatbuffers::Offset<tcp::EnemyTemplateStaticData>> enemies;
    enemies.reserve(enemyTemplates.size());
    for (const EnemyTemplate& enemy : enemyTemplates)
    {
        const auto displayName = builder.CreateString(enemy.name);
        const auto collision = ToTcpCollision(enemy.collision);
        enemies.push_back(tcp::CreateEnemyTemplateStaticData(
            builder,
            enemy.id,
            displayName,
            enemy.maxHp,
            enemy.moveSpeed,
            static_cast<tcp::EnemyAiType>(enemy.aiType),
            &collision));
    }

    const auto roomVector = builder.CreateVector(roomOffsets);
    const auto enemyVector = builder.CreateVector(enemies);
    const auto response = tcp::CreateDungeonStaticDataResponse(
        builder,
        static_cast<tcp::DungeonStaticDataResult>(result),
        dungeonId,
        dungeonTemplateId,
        roomVector,
        enemyVector);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_DungeonStaticDataResponse,
        response.Union());
}

DungeonStaticDataResponseData DecodeDungeonStaticDataResponsePayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_DungeonStaticDataResponse);
    const auto* response = message->payload_as_DungeonStaticDataResponse();
    if (response == nullptr || response->rooms() == nullptr ||
        response->enemy_templates() == nullptr)
    {
        throw std::runtime_error("Invalid dungeon static data response");
    }

    DungeonStaticDataResponseData decoded;
    decoded.result =
        static_cast<DungeonStaticDataResult>(response->result());
    decoded.dungeonId = response->dungeon_id();
    decoded.dungeonTemplateId = response->dungeon_template_id();
    decoded.rooms.reserve(response->rooms()->size());
    decoded.enemyTemplates.reserve(response->enemy_templates()->size());

    for (const auto* source : *response->rooms())
    {
        if (source == nullptr || source->portals() == nullptr ||
            source->obstacles() == nullptr ||
            source->enemy_spawns() == nullptr)
        {
            throw std::runtime_error("Invalid room static data");
        }

        RoomTemplate room;
        room.id = source->room_id();
        room.width = source->width();
        room.depth = source->depth();
        room.playerSpawn = FromTcpPosition(source->player_spawn());

        for (const auto* portalSource : *source->portals())
        {
            if (portalSource == nullptr)
            {
                throw std::runtime_error("Invalid portal static data");
            }

            room.portals.push_back({
                portalSource->portal_id(),
                FromTcpCollision(portalSource->trigger_area()),
                portalSource->target_room_id(),
                FromTcpPosition(portalSource->target_position()),
                portalSource->requires_room_clear()});
        }

        for (const auto* obstacleSource : *source->obstacles())
        {
            if (obstacleSource == nullptr)
            {
                throw std::runtime_error("Invalid obstacle static data");
            }

            room.obstacles.push_back({
                obstacleSource->obstacle_id(),
                FromTcpCollision(obstacleSource->collision()),
                obstacleSource->destructible(),
                obstacleSource->max_hp()});
        }

        for (const auto* spawnSource : *source->enemy_spawns())
        {
            if (spawnSource == nullptr)
            {
                throw std::runtime_error("Invalid enemy spawn static data");
            }

            room.enemySpawns.push_back({
                spawnSource->enemy_spawn_id(),
                spawnSource->enemy_template_id(),
                FromTcpPosition(spawnSource->position()),
                spawnSource->wave()});
        }

        decoded.rooms.push_back(std::move(room));
    }

    for (const auto* source : *response->enemy_templates())
    {
        if (source == nullptr || source->display_name() == nullptr)
        {
            throw std::runtime_error("Invalid enemy template static data");
        }

        EnemyTemplate enemy;
        enemy.id = source->enemy_template_id();
        enemy.name = source->display_name()->str();
        enemy.maxHp = source->max_hp();
        enemy.moveSpeed = source->move_speed();
        enemy.aiType = static_cast<EnemyAiType>(source->ai_type());
        enemy.collision = FromTcpCollision(source->collision());
        decoded.enemyTemplates.push_back(std::move(enemy));
    }

    if (!IsValidStaticData(
            decoded.result,
            decoded.dungeonId,
            decoded.dungeonTemplateId,
            decoded.rooms,
            decoded.enemyTemplates))
    {
        throw std::runtime_error("Invalid dungeon static data values");
    }

    return decoded;
}
} // namespace dnf
