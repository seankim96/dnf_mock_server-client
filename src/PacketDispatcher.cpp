#include "PacketDispatcher.h"

#include "DungeonAdmissionProtocol.h"
#include "DungeonConnectionProtocol.h"
#include "DungeonLifecycleService.h"
#include "DungeonManager.h"
#include "DungeonUdpManager.h"
#include "LoginProtocol.h"
#include "PartyManager.h"
#include "PlayerLoginService.h"

#include <stdexcept>
#include <utility>

namespace dnf
{
namespace
{
LoginResult ToLoginResult(PlayerLoginStatus status)
{
    switch (status)
    {
    case PlayerLoginStatus::Success:
        return LoginSuccess;

    case PlayerLoginStatus::InvalidTicket:
        return InvalidAuthTicket;

    case PlayerLoginStatus::PlayerNotFound:
        return LoginPlayerNotFound;

    case PlayerLoginStatus::ServiceBusy:
        return LoginServiceBusy;

    case PlayerLoginStatus::StorageError:
        return LoginStorageError;
    }

    throw std::runtime_error("Unknown player login status");
}
} // namespace

PacketDispatcher::PacketDispatcher(
    ChannelManager& channelManager,
    PartyManager& partyManager,
    DungeonManager& dungeonManager,
    DungeonUdpManager& dungeonUdpManager,
    PlayerLoginService& playerLoginService,
    SessionId sessionId)
    : partyManager_(partyManager),
      dungeonManager_(dungeonManager),
      dungeonUdpManager_(dungeonUdpManager),
      playerLoginService_(playerLoginService),
      authState_(std::make_shared<SessionAuthState>()),
      sessionId_(sessionId),
      channelPartyRequestHandler_(
          channelManager,
          partyManager,
          sessionId),
      dungeonDataRequestHandler_(dungeonManager, sessionId)
{
}

std::vector<std::uint8_t> PacketDispatcher::Dispatch(
    const Packet& request) const
{
    switch (request.header.type)
    {
    case ChannelListRequest:
    case JoinChannelRequest:
    case CreatePartyRequest:
    case JoinPartyRequest:
    case LeavePartyRequest:
    case PartySnapshotRequest:
        return channelPartyRequestHandler_.Dispatch(request);

    case EnterDungeonRequest:
        return HandleEnterDungeonRequest(request);

    case DungeonConnectionInfoRequest:
        return HandleDungeonConnectionInfoRequest(request);

    case DungeonCatalogRequest:
    case DungeonStaticDataRequest:
        return dungeonDataRequestHandler_.Dispatch(request);

    default:
        throw std::runtime_error("No handler for packet type");
    }
}

void PacketDispatcher::DispatchAsync(
    Packet request,
    ResponseHandler responseHandler) const
{
    if (!responseHandler)
    {
        throw std::invalid_argument("Response handler is required");
    }

    if (request.header.type == LoginRequest)
    {
        if (authState_->IsAuthenticated())
        {
            throw std::runtime_error("Session is already authenticated");
        }

        HandleLoginRequestAsync(
            std::move(request),
            std::move(responseHandler));
        return;
    }

    if (!authState_->IsAuthenticated())
    {
        throw std::runtime_error("Authentication is required");
    }

    responseHandler(Dispatch(request));
}

std::optional<SessionAuthSnapshot> PacketDispatcher::AuthSnapshot() const
{
    return authState_->Snapshot();
}

void PacketDispatcher::HandleLoginRequestAsync(
    Packet request,
    ResponseHandler responseHandler) const
{
    std::string authTicket =
        DecodeLoginRequestPayload(request.payload);
    const std::uint32_t requestId = request.header.requestId;
    const SessionId sessionId = sessionId_;
    const std::shared_ptr<SessionAuthState> authState = authState_;

    playerLoginService_.Login(
        std::move(authTicket),
        [requestId,
         sessionId,
         authState,
         responseHandler = std::move(responseHandler)](
            PlayerLoginResult loginResult) mutable
        {
            LoginResult result = ToLoginResult(loginResult.status);
            if (result == LoginSuccess &&
                (!loginResult.authContext.has_value() ||
                 !loginResult.profile.has_value() ||
                 !authState->Authenticate(
                     loginResult.authContext.value(),
                     loginResult.profile.value())))
            {
                result = LoginStorageError;
            }

            const SessionId responseSessionId =
                result == LoginSuccess ? sessionId : 0;
            const auto responsePayload = EncodeLoginResponsePayload(
                result,
                responseSessionId);

            responseHandler(EncodePacket(
                LoginResponse,
                requestId,
                responsePayload));
        });
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
    case CreateDungeonStatus::ServerStopping:
        result = EnterDungeonResult::UdpAllocationFailed;
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
    std::shared_ptr<DungeonInstance> dungeon;

    if (partyId.has_value())
    {
        dungeon = dungeonManager_.FindDungeonByParty(partyId.value());
    }

    // 재접속 세션은 파티에서 이미 제거됐어도 던전 참가자 상태를 유지한다.
    if (dungeon == nullptr)
    {
        dungeon = dungeonManager_.FindDungeonByParticipant(sessionId_);
    }

    if (dungeon == nullptr)
    {
        if (partyId.has_value())
        {
            result = DungeonConnectionInfoResult::DungeonNotFound;
        }
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

    return EncodePacket(
        DungeonConnectionInfoResponse,
        request.header.requestId,
        EncodeDungeonConnectionInfoResponsePayload(
            result,
            dungeonId,
            udpPort,
            udpToken));
}

} // namespace dnf
