#include "ChannelPartyRequestHandler.h"

#include "ChannelManager.h"
#include "ChannelProtocol.h"
#include "PartyManager.h"
#include "PartyProtocol.h"

#include <stdexcept>

namespace dnf
{
ChannelPartyRequestHandler::ChannelPartyRequestHandler(
    ChannelManager& channelManager,
    PartyManager& partyManager,
    SessionId sessionId)
    : channelManager_(channelManager),
      partyManager_(partyManager),
      sessionId_(sessionId)
{
}

std::vector<std::uint8_t> ChannelPartyRequestHandler::Dispatch(
    const Packet& request) const
{
    switch (request.header.type)
    {
    case ChannelListRequest:
        return HandleChannelListRequest(request);

    case JoinChannelRequest:
        return HandleJoinChannelRequest(request);

    case CreatePartyRequest:
        return HandleCreatePartyRequest(request);

    case JoinPartyRequest:
        return HandleJoinPartyRequest(request);

    case LeavePartyRequest:
        return HandleLeavePartyRequest(request);

    case PartySnapshotRequest:
        return HandlePartySnapshotRequest(request);

    default:
        throw std::runtime_error(
            "No channel or party handler for packet type");
    }
}

std::vector<std::uint8_t>
ChannelPartyRequestHandler::HandleChannelListRequest(
    const Packet& request) const
{
    ValidateChannelListRequestPayload(request.payload);

    const auto channels = channelManager_.GetChannelList();
    const auto responsePayload = EncodeChannelListResponsePayload(channels);

    return EncodePacket(
        ChannelListResponse,
        request.header.requestId,
        responsePayload);
}

std::vector<std::uint8_t>
ChannelPartyRequestHandler::HandleJoinChannelRequest(
    const Packet& request) const
{
    const ChannelId channelId =
        DecodeJoinChannelRequestPayload(request.payload);
    const JoinChannelResult result =
        channelManager_.JoinChannel(sessionId_, channelId);
    const ChannelId joinedChannelId =
        result == JoinChannelResult::Success ? channelId : 0;
    const auto responsePayload =
        EncodeJoinChannelResponsePayload(result, joinedChannelId);

    return EncodePacket(
        JoinChannelResponse,
        request.header.requestId,
        responsePayload);
}

std::vector<std::uint8_t>
ChannelPartyRequestHandler::HandleCreatePartyRequest(
    const Packet& request) const
{
    ValidateCreatePartyRequestPayload(request.payload);

    const auto createdPartyId = partyManager_.CreateParty(sessionId_);
    CreatePartyResult result = CreatePartyResult::AlreadyInParty;
    PartyId partyId = 0;
    SessionId leaderSessionId = 0;

    if (createdPartyId.has_value())
    {
        result = CreatePartyResult::Success;
        partyId = createdPartyId.value();
        leaderSessionId = sessionId_;
    }

    return EncodePacket(
        CreatePartyResponse,
        request.header.requestId,
        EncodeCreatePartyResponsePayload(
            result,
            partyId,
            leaderSessionId));
}

std::vector<std::uint8_t>
ChannelPartyRequestHandler::HandleJoinPartyRequest(
    const Packet& request) const
{
    const PartyId requestedPartyId =
        DecodeJoinPartyRequestPayload(request.payload);
    const JoinPartyResult result =
        partyManager_.JoinParty(requestedPartyId, sessionId_);

    PartyId partyId = 0;
    SessionId leaderSessionId = 0;

    if (result == JoinPartyResult::Success)
    {
        const auto party = partyManager_.GetParty(requestedPartyId);
        if (!party.has_value())
        {
            throw std::runtime_error("Joined party was not found");
        }

        partyId = party->id;
        leaderSessionId = party->leaderSessionId;
    }

    return EncodePacket(
        JoinPartyResponse,
        request.header.requestId,
        EncodeJoinPartyResponsePayload(
            result,
            partyId,
            leaderSessionId));
}

std::vector<std::uint8_t>
ChannelPartyRequestHandler::HandleLeavePartyRequest(
    const Packet& request) const
{
    ValidateLeavePartyRequestPayload(request.payload);

    const bool leftParty = partyManager_.LeaveParty(sessionId_);
    const LeavePartyResult result = leftParty
        ? LeavePartyResult::Success
        : LeavePartyResult::NotInParty;

    return EncodePacket(
        LeavePartyResponse,
        request.header.requestId,
        EncodeLeavePartyResponsePayload(result));
}

std::vector<std::uint8_t>
ChannelPartyRequestHandler::HandlePartySnapshotRequest(
    const Packet& request) const
{
    ValidatePartySnapshotRequestPayload(request.payload);

    PartySnapshotResult result = PartySnapshotResult::NotInParty;
    PartyId partyId = 0;
    SessionId leaderSessionId = 0;
    std::vector<SessionId> members;

    const auto joinedPartyId = partyManager_.GetJoinedParty(sessionId_);
    if (joinedPartyId.has_value())
    {
        const auto party = partyManager_.GetParty(joinedPartyId.value());
        if (party.has_value())
        {
            result = PartySnapshotResult::Success;
            partyId = party->id;
            leaderSessionId = party->leaderSessionId;
            members = party->members;
        }
    }

    return EncodePacket(
        PartySnapshotResponse,
        request.header.requestId,
        EncodePartySnapshotResponsePayload(
            result,
            partyId,
            leaderSessionId,
            members));
}
} // namespace dnf
