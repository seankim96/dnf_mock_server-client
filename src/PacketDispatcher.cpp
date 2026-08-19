#include "PacketDispatcher.h"

#include "ChannelProtocol.h"
#include "DungeonAdmissionProtocol.h"
#include "DungeonConnectionProtocol.h"
#include "DungeonLifecycleService.h"
#include "DungeonManager.h"
#include "DungeonUdpManager.h"
#include "LoginProtocol.h"
#include "LoginValidator.h"
#include "PartyManager.h"
#include "PartyProtocol.h"

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

    case DungeonConnectionInfoRequest:
        return HandleDungeonConnectionInfoRequest(request);

    case CreatePartyRequest:
        return HandleCreatePartyRequest(request);

    case JoinPartyRequest:
        return HandleJoinPartyRequest(request);

    default:
        throw std::runtime_error("No handler for packet type");
    }
}

std::vector<std::uint8_t> PacketDispatcher::HandleLoginRequest(
    const Packet& request) const
{
    const LoginValidator validator;
    const LoginValidationResult validation = validator.Validate(request.payload);

    const SessionId responseSessionId =
        validation.result == LoginSuccess ? sessionId_ : 0;
    const auto responsePayload = EncodeLoginResponsePayload(
        validation.result,
        responseSessionId);

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
                0,
                0));
    }

    const CreateDungeonResult creation =
        dungeonManager_.CreateDungeon(partyId.value(), templateId);

    EnterDungeonResult result = EnterDungeonResult::Success;
    DungeonId dungeonId = 0;
    std::uint16_t udpPort = 0;
    DungeonUdpToken udpToken = 0;

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
        const auto allocatedPort = dungeonUdpManager_.Allocate(
            dungeonId,
            creation.dungeon->Participants());

        bool udpReady = false;
        if (allocatedPort.has_value())
        {
            udpPort = allocatedPort.value();
            const auto allocatedToken =
                dungeonUdpManager_.FindToken(dungeonId, sessionId_);

            if (allocatedToken.has_value())
            {
                udpToken = allocatedToken.value();
                udpReady = true;
            }
        }

        if (!udpReady)
        {
            DungeonLifecycleService lifecycleService(
                dungeonManager_,
                dungeonUdpManager_);

            if (!lifecycleService.CancelWaitingDungeon(dungeonId))
            {
                throw std::runtime_error(
                    "Failed to cancel dungeon after UDP allocation error");
            }

            result = EnterDungeonResult::UdpAllocationFailed;
            dungeonId = 0;
            udpPort = 0;
        }
    }

    return EncodePacket(
        EnterDungeonResponse,
        request.header.requestId,
        EncodeEnterDungeonResponsePayload(
            result,
            dungeonId,
            udpPort,
            udpToken));
}

std::vector<std::uint8_t>
PacketDispatcher::HandleDungeonConnectionInfoRequest(
    const Packet& request) const
{
    ValidateDungeonConnectionInfoRequestPayload(request.payload);

    DungeonConnectionInfoResult result =
        DungeonConnectionInfoResult::NotInParty;
    DungeonId dungeonId = 0;
    std::uint16_t udpPort = 0;
    DungeonUdpToken udpToken = 0;

    const auto partyId = partyManager_.GetJoinedParty(sessionId_);
    if (partyId.has_value())
    {
        const auto dungeon =
            dungeonManager_.FindDungeonByParty(partyId.value());

        if (dungeon == nullptr)
        {
            result = DungeonConnectionInfoResult::DungeonNotFound;
        }
        else if (!dungeon->HasParticipant(sessionId_))
        {
            result = DungeonConnectionInfoResult::NotDungeonParticipant;
        }
        else
        {
            const auto port = dungeonUdpManager_.FindPort(dungeon->Id());
            const auto token =
                dungeonUdpManager_.FindToken(dungeon->Id(), sessionId_);

            if (port.has_value() && token.has_value())
            {
                result = DungeonConnectionInfoResult::Success;
                dungeonId = dungeon->Id();
                udpPort = port.value();
                udpToken = token.value();
            }
            else
            {
                result = DungeonConnectionInfoResult::UdpNotReady;
            }
        }
    }

    return EncodePacket(
        DungeonConnectionInfoResponse,
        request.header.requestId,
        EncodeDungeonConnectionInfoResponsePayload(
            result,
            dungeonId,
            udpPort,
            udpToken));
}

std::vector<std::uint8_t> PacketDispatcher::HandleCreatePartyRequest(
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

std::vector<std::uint8_t> PacketDispatcher::HandleJoinPartyRequest(
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
} // namespace dnf
