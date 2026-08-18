#pragma once

#include "DungeonCatalog.h"
#include "DungeonInstance.h"

#include <cstdint>
#include <vector>

namespace dnf
{
enum class EnterDungeonResult : std::uint8_t
{
    Success,
    NotInParty,
    NotPartyLeader,
    DungeonTemplateNotFound,
    PartyAlreadyInDungeon,
    UdpAllocationFailed
};

struct EnterDungeonResponseData
{
    EnterDungeonResult result = EnterDungeonResult::NotInParty;
    DungeonId dungeonId = 0;
    std::uint16_t udpPort = 0;
};

std::vector<std::uint8_t> EncodeEnterDungeonRequestPayload(
    DungeonTemplateId templateId);

DungeonTemplateId DecodeEnterDungeonRequestPayload(
    const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> EncodeEnterDungeonResponsePayload(
    EnterDungeonResult result,
    DungeonId dungeonId,
    std::uint16_t udpPort);

EnterDungeonResponseData DecodeEnterDungeonResponsePayload(
    const std::vector<std::uint8_t>& payload);
} // namespace dnf
