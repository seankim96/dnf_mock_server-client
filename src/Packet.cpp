#include "Packet.h"

#include <stdexcept>

namespace dnf
{
namespace
{
bool IsValidPacketType(std::uint16_t type)
{
    return type == LoginRequest ||
           type == ChannelListRequest ||
           type == JoinChannelRequest ||
           type == EnterDungeonRequest ||
           type == LoginResponse ||
           type == ChannelListResponse ||
           type == JoinChannelResponse ||
           type == EnterDungeonResponse;
}

void ValidateHeader(std::uint16_t packetSize, std::uint16_t type)
{
    if (packetSize < PACKET_HEADER_SIZE || packetSize > MAX_PACKET_SIZE)
    {
        throw std::runtime_error("Invalid packet size");
    }

    if (!IsValidPacketType(type))
    {
        throw std::runtime_error("Invalid packet type");
    }
}
} // namespace

std::array<std::uint8_t, PACKET_HEADER_SIZE> EncodeHeader(
    const PacketHeader& header)
{
    ValidateHeader(header.packetSize, header.type);

    std::array<std::uint8_t, PACKET_HEADER_SIZE> bytes{};

    // packetSize: 2바이트
    bytes[0] = static_cast<std::uint8_t>(header.packetSize >> 8);
    bytes[1] = static_cast<std::uint8_t>(header.packetSize);

    // type: 2바이트
    const std::uint16_t type = header.type;
    bytes[2] = static_cast<std::uint8_t>(type >> 8);
    bytes[3] = static_cast<std::uint8_t>(type);

    // requestId: 4바이트
    bytes[4] = static_cast<std::uint8_t>(header.requestId >> 24);
    bytes[5] = static_cast<std::uint8_t>(header.requestId >> 16);
    bytes[6] = static_cast<std::uint8_t>(header.requestId >> 8);
    bytes[7] = static_cast<std::uint8_t>(header.requestId);

    return bytes;
}

PacketHeader DecodeHeader(
    const std::array<std::uint8_t, PACKET_HEADER_SIZE>& bytes)
{
    const std::uint16_t packetSize =
        static_cast<std::uint16_t>((bytes[0] << 8) | bytes[1]);

    const std::uint16_t type =
        static_cast<std::uint16_t>((bytes[2] << 8) | bytes[3]);

    const std::uint32_t requestId =
        (static_cast<std::uint32_t>(bytes[4]) << 24) |
        (static_cast<std::uint32_t>(bytes[5]) << 16) |
        (static_cast<std::uint32_t>(bytes[6]) << 8) |
        static_cast<std::uint32_t>(bytes[7]);

    ValidateHeader(packetSize, type);

    PacketHeader header;
    header.packetSize = packetSize;
    header.type = static_cast<PacketType>(type);
    header.requestId = requestId;
    return header;
}

std::vector<std::uint8_t> EncodePacket(
    PacketType type,
    std::uint32_t requestId,
    const std::vector<std::uint8_t>& payload)
{
    const std::size_t packetSize = PACKET_HEADER_SIZE + payload.size();
    if (packetSize > MAX_PACKET_SIZE)
    {
        throw std::runtime_error("Packet is too large");
    }

    PacketHeader header;
    header.packetSize = static_cast<std::uint16_t>(packetSize);
    header.type = type;
    header.requestId = requestId;

    const auto headerBytes = EncodeHeader(header);
    std::vector<std::uint8_t> packetBytes(
        headerBytes.begin(), headerBytes.end());
    packetBytes.insert(packetBytes.end(), payload.begin(), payload.end());
    return packetBytes;
}
} // namespace dnf
