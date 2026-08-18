#include "DungeonConnectionProtocol.h"

#include <stdexcept>

namespace dnf
{
namespace
{
void AppendUint16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void AppendUint64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

std::uint16_t ReadUint16(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset)
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8) |
        bytes[offset + 1]);
}

std::uint64_t ReadUint64(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset)
{
    std::uint64_t value = 0;

    for (std::size_t index = 0; index < 8; ++index)
    {
        value = (value << 8) | bytes[offset + index];
    }

    return value;
}

bool IsValidResponse(
    DungeonConnectionInfoResult result,
    DungeonId dungeonId,
    std::uint16_t udpPort,
    DungeonUdpToken udpToken)
{
    if (result < DungeonConnectionInfoResult::Success ||
        result > DungeonConnectionInfoResult::UdpNotReady)
    {
        return false;
    }

    const bool succeeded =
        result == DungeonConnectionInfoResult::Success;
    return succeeded == (dungeonId != 0) &&
           succeeded == (udpPort != 0) &&
           succeeded == (udpToken != 0);
}
} // namespace

void ValidateDungeonConnectionInfoRequestPayload(
    const std::vector<std::uint8_t>& payload)
{
    if (!payload.empty())
    {
        throw std::runtime_error(
            "Dungeon connection info request payload must be empty");
    }
}

std::vector<std::uint8_t> EncodeDungeonConnectionInfoResponsePayload(
    DungeonConnectionInfoResult result,
    DungeonId dungeonId,
    std::uint16_t udpPort,
    DungeonUdpToken udpToken)
{
    if (!IsValidResponse(result, dungeonId, udpPort, udpToken))
    {
        throw std::invalid_argument(
            "Invalid dungeon connection info response");
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(19);
    payload.push_back(static_cast<std::uint8_t>(result));
    AppendUint64(payload, dungeonId);
    AppendUint16(payload, udpPort);
    AppendUint64(payload, udpToken);
    return payload;
}

DungeonConnectionInfoData DecodeDungeonConnectionInfoResponsePayload(
    const std::vector<std::uint8_t>& payload)
{
    if (payload.size() != 19)
    {
        throw std::runtime_error(
            "Invalid dungeon connection info response payload");
    }

    const auto result =
        static_cast<DungeonConnectionInfoResult>(payload[0]);
    const DungeonId dungeonId = ReadUint64(payload, 1);
    const std::uint16_t udpPort = ReadUint16(payload, 9);
    const DungeonUdpToken udpToken = ReadUint64(payload, 11);

    if (!IsValidResponse(result, dungeonId, udpPort, udpToken))
    {
        throw std::runtime_error(
            "Invalid dungeon connection info response data");
    }

    return {result, dungeonId, udpPort, udpToken};
}
} // namespace dnf
