#pragma once

#include "DungeonInstance.h"
#include "DungeonUdpTypes.h"

#include <cstdint>
#include <vector>

namespace dnf
{
enum class DungeonConnectionInfoResult : std::uint8_t
{
    Success,
    NotInParty,
    DungeonNotFound,
    NotDungeonParticipant,
    UdpNotReady
};

struct DungeonConnectionInfoData
{
    DungeonConnectionInfoResult result =
        DungeonConnectionInfoResult::NotInParty;
    DungeonId dungeonId = 0;
    std::uint16_t udpPort = 0;
    DungeonUdpToken udpToken = 0;
};

void ValidateDungeonConnectionInfoRequestPayload(
    const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> EncodeDungeonConnectionInfoResponsePayload(
    DungeonConnectionInfoResult result,
    DungeonId dungeonId,
    std::uint16_t udpPort,
    DungeonUdpToken udpToken);

DungeonConnectionInfoData DecodeDungeonConnectionInfoResponsePayload(
    const std::vector<std::uint8_t>& payload);
} // namespace dnf
