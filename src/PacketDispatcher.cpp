#include "PacketDispatcher.h"

#include "ChannelProtocol.h"
#include "DungeonAdmissionProtocol.h"
#include "DungeonManager.h"
#include "DungeonUdpManager.h"
#include "LoginValidator.h"
#include "PartyManager.h"

#include <stdexcept>

namespace dnf
{
PacketDispatcher::PacketDispatcher(
    ChannelManager& channelManager,
    PartyManager& partyManager,
    DungeonManager& dungeonManager,
    DungeonUdpManager& dungeonUdpManager,
    SessionId sessionId)
    : channelManager_(channelManager),
      partyManager_(partyManager),
      dungeonManager_(dungeonManager),
      dungeonUdpManager_(dungeonUdpManager),
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

    case EnterDungeonRequest:
        return HandleEnterDungeonRequest(request);

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

std::vector<std::uint8_t> PacketDispatcher::HandleEnterDungeonRequest(
    const Packet& request) const
{
    const DungeonTemplateId templateId =
        DecodeEnterDungeonRequestPayload(request.payload);

    const auto partyId = partyManager_.GetJoinedParty(sessionId_);
    if (!partyId.has_value())
    {
        return EncodePacket(
            EnterDungeonResponse,
            request.header.requestId,
            EncodeEnterDungeonResponsePayload(
                EnterDungeonResult::NotInParty,
                0,
                0));
    }

    const auto party = partyManager_.GetParty(partyId.value());
    if (!party.has_value())
    {
        return EncodePacket(
            EnterDungeonResponse,
            request.header.requestId,
            EncodeEnterDungeonResponsePayload(
                EnterDungeonResult::NotInParty,
                0,
                0));
    }

    if (party->leaderSessionId != sessionId_)
    {
        return EncodePacket(
            EnterDungeonResponse,
            request.header.requestId,
            EncodeEnterDungeonResponsePayload(
                EnterDungeonResult::NotPartyLeader,
                0,
                0));
    }

    const CreateDungeonResult creation =
        dungeonManager_.CreateDungeon(partyId.value(), templateId);

    EnterDungeonResult result = EnterDungeonResult::Success;
    DungeonId dungeonId = 0;
    std::uint16_t udpPort = 0;

    switch (creation.status)
    {
    case CreateDungeonStatus::Success:
        dungeonId = creation.dungeon->Id();
        break;
    case CreateDungeonStatus::PartyNotFound:
        result = EnterDungeonResult::NotInParty;
        break;
    case CreateDungeonStatus::DungeonTemplateNotFound:
        result = EnterDungeonResult::DungeonTemplateNotFound;
        break;
    case CreateDungeonStatus::PartyAlreadyInDungeon:
        result = EnterDungeonResult::PartyAlreadyInDungeon;
        break;
    }

    if (result == EnterDungeonResult::Success)
    {
        const auto allocatedPort = dungeonUdpManager_.Allocate(dungeonId);
        if (allocatedPort.has_value())
        {
            udpPort = allocatedPort.value();
        }
        else
        {
            if (!dungeonManager_.CancelDungeon(dungeonId))
            {
                throw std::runtime_error(
                    "Failed to cancel dungeon after UDP allocation error");
            }

            result = EnterDungeonResult::UdpAllocationFailed;
            dungeonId = 0;
        }
    }

    return EncodePacket(
        EnterDungeonResponse,
        request.header.requestId,
        EncodeEnterDungeonResponsePayload(result, dungeonId, udpPort));
}
} // namespace dnf
