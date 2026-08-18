#include "ChannelProtocol.h"

#include <limits>
#include <stdexcept>

namespace dnf
{
namespace
{
constexpr std::size_t CHANNEL_ENTRY_SIZE = 12;

void AppendUint16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void AppendUint32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

std::uint16_t ReadUint16(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset)
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8) |
        bytes[offset + 1]);
}

std::uint32_t ReadUint32(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset)
{
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}
} // namespace

std::vector<std::uint8_t> EncodeChannelListPayload(
    const std::vector<ChannelInfo>& channels)
{
    if (channels.size() > std::numeric_limits<std::uint16_t>::max())
    {
        throw std::runtime_error("Too many channels");
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(2 + channels.size() * CHANNEL_ENTRY_SIZE);
    AppendUint16(payload, static_cast<std::uint16_t>(channels.size()));

    for (const ChannelInfo& channel : channels)
    {
        if (channel.currentPlayers > std::numeric_limits<std::uint32_t>::max() ||
            channel.maxPlayers > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::runtime_error("Channel player count is too large");
        }

        AppendUint32(payload, channel.id);
        AppendUint32(
            payload,
            static_cast<std::uint32_t>(channel.currentPlayers));
        AppendUint32(
            payload,
            static_cast<std::uint32_t>(channel.maxPlayers));
    }

    return payload;
}

std::vector<ChannelListEntry> DecodeChannelListPayload(
    const std::vector<std::uint8_t>& payload)
{
    if (payload.size() < 2)
    {
        throw std::runtime_error("Invalid channel list payload");
    }

    const std::uint16_t channelCount = ReadUint16(payload, 0);
    const std::size_t expectedSize =
        2 + static_cast<std::size_t>(channelCount) * CHANNEL_ENTRY_SIZE;

    if (payload.size() != expectedSize)
    {
        throw std::runtime_error("Invalid channel list payload size");
    }

    std::vector<ChannelListEntry> channels;
    channels.reserve(channelCount);

    std::size_t offset = 2;
    for (std::uint16_t index = 0; index < channelCount; ++index)
    {
        ChannelListEntry channel;
        channel.id = ReadUint32(payload, offset);
        channel.currentPlayers = ReadUint32(payload, offset + 4);
        channel.maxPlayers = ReadUint32(payload, offset + 8);
        channels.push_back(channel);

        offset += CHANNEL_ENTRY_SIZE;
    }

    return channels;
}

std::vector<std::uint8_t> EncodeJoinChannelRequestPayload(
    ChannelId channelId)
{
    std::vector<std::uint8_t> payload;
    payload.reserve(4);
    AppendUint32(payload, channelId);
    return payload;
}

ChannelId DecodeJoinChannelRequestPayload(
    const std::vector<std::uint8_t>& payload)
{
    if (payload.size() != 4)
    {
        throw std::runtime_error("Invalid join channel request payload");
    }

    return ReadUint32(payload, 0);
}

std::vector<std::uint8_t> EncodeJoinChannelResponsePayload(
    JoinChannelResult result,
    ChannelId channelId)
{
    std::vector<std::uint8_t> payload;
    payload.reserve(5);
    payload.push_back(static_cast<std::uint8_t>(result));
    AppendUint32(payload, channelId);
    return payload;
}

JoinChannelResponseData DecodeJoinChannelResponsePayload(
    const std::vector<std::uint8_t>& payload)
{
    if (payload.size() != 5 ||
        payload[0] > static_cast<std::uint8_t>(JoinChannelResult::AlreadyJoined))
    {
        throw std::runtime_error("Invalid join channel response payload");
    }

    JoinChannelResponseData response;
    response.result = static_cast<JoinChannelResult>(payload[0]);
    response.channelId = ReadUint32(payload, 1);
    return response;
}
} // namespace dnf
