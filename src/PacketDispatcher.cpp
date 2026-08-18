#include "PacketDispatcher.h"

#include "ChannelProtocol.h"
#include "LoginValidator.h"

#include <stdexcept>

namespace dnf
{
PacketDispatcher::PacketDispatcher(
    ChannelManager& channelManager,
    SessionId sessionId)
    : channelManager_(channelManager),
      sessionId_(sessionId)
{
}

std::vector<std::uint8_t> PacketDispatcher::Dispatch(
    const Packet& request) const
{
    switch (request.header.type)
    {
    case LoginRequest:
        return HandleLoginRequest(request);

    case ChannelListRequest:
        return HandleChannelListRequest(request);

    case JoinChannelRequest:
        return HandleJoinChannelRequest(request);

    default:
        throw std::runtime_error("No handler for packet type");
    }
}

std::vector<std::uint8_t> PacketDispatcher::HandleLoginRequest(
    const Packet& request) const
{
    const LoginValidator validator;
    const LoginValidationResult validation = validator.Validate(request.payload);

    const std::vector<std::uint8_t> responsePayload = {
        static_cast<std::uint8_t>(validation.result)};

    return EncodePacket(
        LoginResponse,
        request.header.requestId,
        responsePayload);
}

std::vector<std::uint8_t> PacketDispatcher::HandleChannelListRequest(
    const Packet& request) const
{
    if (!request.payload.empty())
    {
        throw std::runtime_error("ChannelListRequest payload must be empty");
    }

    const auto channels = channelManager_.GetChannelList();
    const auto responsePayload = EncodeChannelListPayload(channels);

    return EncodePacket(
        ChannelListResponse,
        request.header.requestId,
        responsePayload);
}

std::vector<std::uint8_t> PacketDispatcher::HandleJoinChannelRequest(
    const Packet& request) const
{
    const ChannelId channelId =
        DecodeJoinChannelRequestPayload(request.payload);
    const JoinChannelResult result =
        channelManager_.JoinChannel(sessionId_, channelId);
    const auto responsePayload =
        EncodeJoinChannelResponsePayload(result, channelId);

    return EncodePacket(
        JoinChannelResponse,
        request.header.requestId,
        responsePayload);
}
} // namespace dnf
