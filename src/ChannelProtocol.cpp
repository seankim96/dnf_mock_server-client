#include "ChannelProtocol.h"
#include "TcpFlatBufferCodec.h"

#include <flatbuffers/flatbuffer_builder.h>

#include <limits>
#include <stdexcept>

namespace dnf
{
namespace
{
namespace tcp = Dnf::Protocol::Tcp;

bool IsValidJoinChannelResponse(
    JoinChannelResult result,
    ChannelId channelId)
{
    if (result < JoinChannelResult::Success ||
        result > JoinChannelResult::AlreadyJoined)
    {
        return false;
    }

    const bool succeeded = result == JoinChannelResult::Success;
    return succeeded == (channelId != 0);
}
} // namespace

void ValidateChannelListRequestPayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_ChannelListRequest);
    if (message->payload_as_ChannelListRequest() == nullptr)
    {
        throw std::runtime_error("Invalid channel list request payload");
    }
}

std::vector<std::uint8_t> EncodeChannelListRequestPayload()
{
    flatbuffers::FlatBufferBuilder builder;
    const auto request = tcp::CreateChannelListRequest(builder);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_ChannelListRequest,
        request.Union());
}

std::vector<std::uint8_t> EncodeChannelListResponsePayload(
    const std::vector<ChannelInfo>& channels)
{
    flatbuffers::FlatBufferBuilder builder;
    std::vector<flatbuffers::Offset<tcp::ChannelInfo>> entries;
    entries.reserve(channels.size());

    for (const ChannelInfo& channel : channels)
    {
        if (channel.id == 0 || channel.name.empty() ||
            channel.maxPlayers == 0 ||
            channel.currentPlayers > channel.maxPlayers ||
            channel.currentPlayers > std::numeric_limits<std::uint32_t>::max() ||
            channel.maxPlayers > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::invalid_argument("Invalid channel list entry");
        }

        const auto name = builder.CreateString(channel.name);
        entries.push_back(tcp::CreateChannelInfo(
            builder,
            channel.id,
            name,
            static_cast<std::uint32_t>(channel.currentPlayers),
            static_cast<std::uint32_t>(channel.maxPlayers)));
    }

    const auto channelEntries = builder.CreateVector(entries);
    const auto response = tcp::CreateChannelListResponse(
        builder,
        channelEntries);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_ChannelListResponse,
        response.Union());
}

std::vector<ChannelListEntry> DecodeChannelListResponsePayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_ChannelListResponse);
    const auto* response = message->payload_as_ChannelListResponse();
    if (response == nullptr || response->channels() == nullptr)
    {
        throw std::runtime_error("Invalid channel list response payload");
    }

    std::vector<ChannelListEntry> channels;
    channels.reserve(response->channels()->size());

    for (const auto* source : *response->channels())
    {
        if (source == nullptr || source->display_name() == nullptr ||
            source->channel_id() == 0 || source->display_name()->empty() ||
            source->max_players() == 0 ||
            source->current_players() > source->max_players())
        {
            throw std::runtime_error("Invalid channel list response data");
        }

        channels.push_back({
            source->channel_id(),
            source->display_name()->str(),
            source->current_players(),
            source->max_players()});
    }

    return channels;
}

std::vector<std::uint8_t> EncodeJoinChannelRequestPayload(
    ChannelId channelId)
{
    if (channelId == 0)
    {
        throw std::invalid_argument("Channel ID must not be zero");
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto request = tcp::CreateJoinChannelRequest(builder, channelId);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_JoinChannelRequest,
        request.Union());
}

ChannelId DecodeJoinChannelRequestPayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_JoinChannelRequest);
    const auto* request = message->payload_as_JoinChannelRequest();
    if (request == nullptr || request->channel_id() == 0)
    {
        throw std::runtime_error("Invalid join channel request data");
    }

    return request->channel_id();
}

std::vector<std::uint8_t> EncodeJoinChannelResponsePayload(
    JoinChannelResult result,
    ChannelId channelId)
{
    if (!IsValidJoinChannelResponse(result, channelId))
    {
        throw std::invalid_argument("Invalid join channel response");
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto response = tcp::CreateJoinChannelResponse(
        builder,
        static_cast<tcp::JoinChannelResult>(result),
        channelId);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_JoinChannelResponse,
        response.Union());
}

JoinChannelResponseData DecodeJoinChannelResponsePayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_JoinChannelResponse);
    const auto* response = message->payload_as_JoinChannelResponse();
    if (response == nullptr)
    {
        throw std::runtime_error("Invalid join channel response payload");
    }

    const auto result =
        static_cast<JoinChannelResult>(response->result());
    const ChannelId channelId = response->channel_id();
    if (!IsValidJoinChannelResponse(result, channelId))
    {
        throw std::runtime_error("Invalid join channel response data");
    }

    return {result, channelId};
}
} // namespace dnf
