#include "DungeonDataRequestHandler.h"

#include "DungeonCatalogProtocol.h"
#include "DungeonManager.h"
#include "DungeonStaticDataProtocol.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace dnf
{
DungeonDataRequestHandler::DungeonDataRequestHandler(
    DungeonManager& dungeonManager,
    SessionId sessionId)
    : dungeonManager_(dungeonManager),
      sessionId_(sessionId)
{
}

std::vector<std::uint8_t> DungeonDataRequestHandler::Dispatch(
    const Packet& request) const
{
    switch (request.header.type)
    {
    case DungeonCatalogRequest:
        return HandleDungeonCatalogRequest(request);

    case DungeonStaticDataRequest:
        return HandleDungeonStaticDataRequest(request);

    default:
        throw std::runtime_error(
            "No dungeon data handler for packet type");
    }
}

std::vector<std::uint8_t>
DungeonDataRequestHandler::HandleDungeonCatalogRequest(
    const Packet& request) const
{
    ValidateDungeonCatalogRequestPayload(request.payload);

    const auto dungeons = dungeonManager_.GetDungeonTemplates();
    const auto responsePayload = EncodeDungeonCatalogResponsePayload(
        CatalogResult::Success,
        dungeons);

    return EncodePacket(
        DungeonCatalogResponse,
        request.header.requestId,
        responsePayload);
}

std::vector<std::uint8_t>
DungeonDataRequestHandler::HandleDungeonStaticDataRequest(
    const Packet& request) const
{
    const DungeonId dungeonId =
        DecodeDungeonStaticDataRequestPayload(request.payload);
    const auto dungeon = dungeonManager_.FindDungeon(dungeonId);

    if (dungeon == nullptr)
    {
        return EncodePacket(
            DungeonStaticDataResponse,
            request.header.requestId,
            EncodeDungeonStaticDataResponsePayload(
                DungeonStaticDataResult::DungeonNotFound,
                0,
                0,
                {},
                {}));
    }

    if (!dungeon->HasParticipant(sessionId_))
    {
        return EncodePacket(
            DungeonStaticDataResponse,
            request.header.requestId,
            EncodeDungeonStaticDataResponsePayload(
                DungeonStaticDataResult::NotDungeonParticipant,
                0,
                0,
                {},
                {}));
    }

    const auto dungeonTemplate =
        dungeonManager_.GetDungeonTemplate(dungeon->TemplateId());
    if (!dungeonTemplate.has_value())
    {
        throw std::runtime_error("Dungeon template was not found");
    }

    std::vector<EnemyTemplate> enemyTemplates;
    std::unordered_set<EnemyTemplateId> addedEnemyIds;

    for (const RoomTemplate& room : dungeonTemplate->rooms)
    {
        for (const EnemySpawnTemplate& spawn : room.enemySpawns)
        {
            if (!addedEnemyIds.insert(spawn.enemyTemplateId).second)
            {
                continue;
            }

            const auto enemy =
                dungeonManager_.GetEnemyTemplate(spawn.enemyTemplateId);
            if (!enemy.has_value())
            {
                throw std::runtime_error("Enemy template was not found");
            }

            enemyTemplates.push_back(enemy.value());
        }
    }

    std::sort(
        enemyTemplates.begin(),
        enemyTemplates.end(),
        [](const EnemyTemplate& left, const EnemyTemplate& right)
        {
            return left.id < right.id;
        });

    return EncodePacket(
        DungeonStaticDataResponse,
        request.header.requestId,
        EncodeDungeonStaticDataResponsePayload(
            DungeonStaticDataResult::Success,
            dungeonId,
            dungeonTemplate->id,
            dungeonTemplate->rooms,
            enemyTemplates));
}
} // namespace dnf
