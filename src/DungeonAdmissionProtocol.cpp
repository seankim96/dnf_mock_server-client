#include "DungeonAdmissionProtocol.h"

#include <stdexcept>

namespace dnf
{
namespace
{
void AppendUint32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

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

std::uint32_t ReadUint32(const std::vector<std::uint8_t>& bytes)
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) |
           static_cast<std::uint32_t>(bytes[3]);
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

std::uint16_t ReadUint16(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset)
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8) |
        bytes[offset + 1]);
}

bool IsValidResult(EnterDungeonResult result)
{
    return result >= EnterDungeonResult::Success &&
           result <= EnterDungeonResult::UdpAllocationFailed;
}

bool IsValidResponse(
    EnterDungeonResult result,
    DungeonId dungeonId,
    std::uint16_t udpPort,
    DungeonUdpToken udpToken)
{
    if (!IsValidResult(result))
    {
        return false;
    }

    const bool succeeded = result == EnterDungeonResult::Success;
    return succeeded == (dungeonId != 0) &&
           succeeded == (udpPort != 0) &&
           succeeded == (udpToken != 0);
}
} // namespace

std::vector<std::uint8_t> EncodeEnterDungeonRequestPayload(
    DungeonTemplateId templateId)
{
    if (templateId == 0)
    {
        throw std::invalid_argument("Dungeon template ID must not be zero");
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(4);
    AppendUint32(payload, templateId);
    return payload;
}

DungeonTemplateId DecodeEnterDungeonRequestPayload(
    const std::vector<std::uint8_t>& payload)
{
    if (payload.size() != 4)
    {
        throw std::runtime_error("Invalid enter dungeon request payload");
    }

    const DungeonTemplateId templateId = ReadUint32(payload);
    if (templateId == 0)
    {
        throw std::runtime_error("Invalid dungeon template ID");
    }

    return templateId;
}

std::vector<std::uint8_t> EncodeEnterDungeonResponsePayload(
    EnterDungeonResult result,
    DungeonId dungeonId,
    std::uint16_t udpPort,
    DungeonUdpToken udpToken)
{
    if (!IsValidResponse(result, dungeonId, udpPort, udpToken))
    {
        throw std::invalid_argument("Invalid enter dungeon response");
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(19);
    payload.push_back(static_cast<std::uint8_t>(result));
    AppendUint64(payload, dungeonId);
    AppendUint16(payload, udpPort);
    AppendUint64(payload, udpToken);
    return payload;
}

EnterDungeonResponseData DecodeEnterDungeonResponsePayload(
    const std::vector<std::uint8_t>& payload)
{
    if (payload.size() != 19)
    {
        throw std::runtime_error("Invalid enter dungeon response payload");
    }

    const auto result = static_cast<EnterDungeonResult>(payload[0]);
    const DungeonId dungeonId = ReadUint64(payload, 1);
    const std::uint16_t udpPort = ReadUint16(payload, 9);
    const DungeonUdpToken udpToken = ReadUint64(payload, 11);

    if (!IsValidResponse(result, dungeonId, udpPort, udpToken))
    {
        throw std::runtime_error("Invalid enter dungeon response data");
    }

    return {result, dungeonId, udpPort, udpToken};
}
} // namespace dnf
