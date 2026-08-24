#include "PacketDispatcher.h"

#include "ChannelProtocol.h"
#include "DungeonAdmissionProtocol.h"
#include "DungeonCatalogProtocol.h"
#include "DungeonConnectionProtocol.h"
#include "DungeonLifecycleService.h"
#include "DungeonManager.h"
#include "DungeonStaticDataProtocol.h"
#include "DungeonUdpManager.h"
#include "LoginProtocol.h"
#include "PartyManager.h"
#include "PartyProtocol.h"
#include "PlayerLoginService.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
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
    : channelManager_(channelManager),
      partyManager_(partyManager),
      dungeonManager_(dungeonManager),
      dungeonUdpManager_(dungeonUdpManager),
      playerLoginService_(playerLoginService),
      authState_(std::make_shared<SessionAuthState>()),
      sessionId_(sessionId)
{
}

std::vector<std::uint8_t> PacketDispatcher::Dispatch(
    const Packet& request) const
{
    switch (request.header.type)
    {
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

    case LeavePartyRequest:
        return HandleLeavePartyRequest(request);

    case PartySnapshotRequest:
        return HandlePartySnapshotRequest(request);

    case DungeonCatalogRequest:
        return HandleDungeonCatalogRequest(request);

    case DungeonStaticDataRequest:
        return HandleDungeonStaticDataRequest(request);

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

std::vector<std::uint8_t> PacketDispatcher::HandleChannelListRequest(
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

std::vector<std::uint8_t> PacketDispatcher::HandleJoinChannelRequest(
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

std::vector<std::uint8_t> PacketDispatcher::HandleLeavePartyRequest(
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

std::vector<std::uint8_t> PacketDispatcher::HandlePartySnapshotRequest(
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

std::vector<std::uint8_t> PacketDispatcher::HandleDungeonCatalogRequest(
    const Packet& request) const
{
    ValidateDungeonCatalogRequestPayload(request.payload);

    const auto dungeons = dungeonManager_.GetDungeonTemplates();
    const auto responsePayload = EncodeDungeonCatalogResponsePayload(
        CatalogResult::Success,
        dungeons);

    return EncodePacket(
        DungeonCatalogResponse,
        request.header.requestId,
        responsePayload);
}

std::vector<std::uint8_t> PacketDispatcher::HandleDungeonStaticDataRequest(
    const Packet& request) const
{
    const DungeonId dungeonId =
        DecodeDungeonStaticDataRequestPayload(request.payload);
    const auto dungeon = dungeonManager_.FindDungeon(dungeonId);

    if (dungeon == nullptr)
    {
        return EncodePacket(
            DungeonStaticDataResponse,
            request.header.requestId,
            EncodeDungeonStaticDataResponsePayload(
                DungeonStaticDataResult::DungeonNotFound,
                0,
                0,
                {},
                {}));
    }

    if (!dungeon->HasParticipant(sessionId_))
    {
        return EncodePacket(
            DungeonStaticDataResponse,
            request.header.requestId,
            EncodeDungeonStaticDataResponsePayload(
                DungeonStaticDataResult::NotDungeonParticipant,
                0,
                0,
                {},
                {}));
    }

    const auto dungeonTemplate =
        dungeonManager_.GetDungeonTemplate(dungeon->TemplateId());
    if (!dungeonTemplate.has_value())
    {
        throw std::runtime_error("Dungeon template was not found");
    }

    std::vector<EnemyTemplate> enemyTemplates;
    std::unordered_set<EnemyTemplateId> addedEnemyIds;

    for (const RoomTemplate& room : dungeonTemplate->rooms)
    {
        for (const EnemySpawnTemplate& spawn : room.enemySpawns)
        {
            if (!addedEnemyIds.insert(spawn.enemyTemplateId).second)
            {
                continue;
            }

            const auto enemy =
                dungeonManager_.GetEnemyTemplate(spawn.enemyTemplateId);
            if (!enemy.has_value())
            {
                throw std::runtime_error("Enemy template was not found");
            }

            enemyTemplates.push_back(enemy.value());
        }
    }

    std::sort(
        enemyTemplates.begin(),
        enemyTemplates.end(),
        [](const EnemyTemplate& left, const EnemyTemplate& right)
        {
            return left.id < right.id;
        });

    return EncodePacket(
        DungeonStaticDataResponse,
        request.header.requestId,
        EncodeDungeonStaticDataResponsePayload(
            DungeonStaticDataResult::Success,
            dungeonId,
            dungeonTemplate->id,
            dungeonTemplate->rooms,
            enemyTemplates));
}
} // namespace dnf
