#pragma once

#include "DungeonCatalog.h"
#include "DungeonInstance.h"
#include "EnemyCatalog.h"

#include <cstdint>
#include <vector>

namespace dnf
{
enum class DungeonStaticDataResult : std::uint8_t
{
    Success = 0,
    DungeonNotFound = 1,
    NotDungeonParticipant = 2
};

struct DungeonStaticDataResponseData
{
    DungeonStaticDataResult result =
        DungeonStaticDataResult::DungeonNotFound;
    DungeonId dungeonId = 0;
    DungeonTemplateId dungeonTemplateId = 0;
    std::vector<RoomTemplate> rooms;
    std::vector<EnemyTemplate> enemyTemplates;
};

std::vector<std::uint8_t> EncodeDungeonStaticDataRequestPayload(
    DungeonId dungeonId);

DungeonId DecodeDungeonStaticDataRequestPayload(
    const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> EncodeDungeonStaticDataResponsePayload(
    DungeonStaticDataResult result,
    DungeonId dungeonId,
    DungeonTemplateId dungeonTemplateId,
    const std::vector<RoomTemplate>& rooms,
    const std::vector<EnemyTemplate>& enemyTemplates);

DungeonStaticDataResponseData DecodeDungeonStaticDataResponsePayload(
    const std::vector<std::uint8_t>& payload);
} // namespace dnf
