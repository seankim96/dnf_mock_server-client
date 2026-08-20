#include "ChannelProtocol.h"
#include "DungeonAdmissionProtocol.h"
#include "DungeonCatalogProtocol.h"
#include "DungeonConnectionProtocol.h"
#include "DungeonManager.h"
#include "DungeonStaticDataProtocol.h"
#include "DungeonUdpManager.h"
#include "EnemyCatalog.h"
#include "LoginProtocol.h"
#include "PacketDispatcher.h"
#include "PartyManager.h"
#include "PartyProtocol.h"
#include "ReceiveBuffer.h"

#include <boost/asio/io_context.hpp>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
struct TestContext
{
    TestContext()
        : dungeonUdpManager(ioContext),
          dungeonCatalog(enemyCatalog),
          dungeonManager(partyManager, dungeonCatalog, enemyCatalog)
    {
        dnf::EnemyTemplate goblin;
        goblin.id = 2001;
        goblin.name = "Goblin";
        goblin.maxHp = 100;
        goblin.moveSpeed = 120.0f;
        goblin.collision = {
            {-20.0f, -15.0f, 0.0f},
            {20.0f, 15.0f, 80.0f}};
        assert(enemyCatalog.AddEnemy(goblin));

        dnf::RoomTemplate room{
            1, 1200.0f, 500.0f, {100.0f, 250.0f, 0.0f}};

        dnf::ObstacleTemplate crate;
        crate.id = 1;
        crate.collision = {
            {500.0f, 100.0f, 0.0f},
            {580.0f, 180.0f, 100.0f}};
        crate.destructible = true;
        crate.maxHp = 100;
        room.obstacles.push_back(crate);

        dnf::EnemySpawnTemplate spawn;
        spawn.id = 1;
        spawn.enemyTemplateId = goblin.id;
        spawn.position = {800.0f, 250.0f, 0.0f};
        room.enemySpawns.push_back(spawn);

        assert(dungeonCatalog.AddDungeon(1001, "Forest", {room}));
    }

    boost::asio::io_context ioContext;
    dnf::DungeonUdpManager dungeonUdpManager;
    dnf::ChannelManager channelManager;
    dnf::PartyManager partyManager;
    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonCatalog dungeonCatalog;
    dnf::DungeonManager dungeonManager;
};

void TestLoginRequest()
{
    TestContext context;
    dnf::Packet request;
    request.header.type = dnf::LoginRequest;
    request.header.requestId = 42;
    request.payload = dnf::EncodeLoginRequestPayload("Mock");

    dnf::PacketDispatcher dispatcher(
        context.channelManager,
        context.partyManager,
        context.dungeonManager,
        context.dungeonUdpManager,
        100);
    const auto responseBytes = dispatcher.Dispatch(request);

    dnf::ReceiveBuffer buffer;
    buffer.Append(responseBytes);

    dnf::Packet response;
    assert(buffer.TryPop(response) == true);
    assert(response.header.type == dnf::LoginResponse);
    assert(response.header.requestId == 42);

    const auto loginResponse =
        dnf::DecodeLoginResponsePayload(response.payload);
    assert(loginResponse.result == dnf::LoginSuccess);
    assert(loginResponse.sessionId == 100);
}

void TestChannelListRequest()
{
    TestContext context;
    context.channelManager.AddChannel(1, "Channel 1", 100);
    context.channelManager.AddChannel(2, "Channel 2", 200);
    context.channelManager.JoinChannel(500, 1);

    dnf::Packet request;
    request.header.type = dnf::ChannelListRequest;
    request.header.requestId = 44;
    request.payload = dnf::EncodeChannelListRequestPayload();

    dnf::PacketDispatcher dispatcher(
        context.channelManager,
        context.partyManager,
        context.dungeonManager,
        context.dungeonUdpManager,
        100);
    const auto responseBytes = dispatcher.Dispatch(request);

    dnf::ReceiveBuffer buffer;
    buffer.Append(responseBytes);

    dnf::Packet response;
    assert(buffer.TryPop(response) == true);
    assert(response.header.type == dnf::ChannelListResponse);
    assert(response.header.requestId == 44);

    const auto channels =
        dnf::DecodeChannelListResponsePayload(response.payload);
    assert(channels.size() == 2);
    assert(channels[0].id == 1);
    assert(channels[0].name == "Channel 1");
    assert(channels[0].currentPlayers == 1);
    assert(channels[0].maxPlayers == 100);
    assert(channels[1].id == 2);
    assert(channels[1].name == "Channel 2");
    assert(channels[1].currentPlayers == 0);
    assert(channels[1].maxPlayers == 200);
}

void TestDungeonCatalogRequest()
{
    TestContext context;

    dnf::Packet request;
    request.header.type = dnf::DungeonCatalogRequest;
    request.header.requestId = 47;
    request.payload = dnf::EncodeDungeonCatalogRequestPayload();

    dnf::PacketDispatcher dispatcher(
        context.channelManager,
        context.partyManager,
        context.dungeonManager,
        context.dungeonUdpManager,
        100);

    dnf::ReceiveBuffer buffer;
    buffer.Append(dispatcher.Dispatch(request));

    dnf::Packet response;
    assert(buffer.TryPop(response));
    assert(response.header.type == dnf::DungeonCatalogResponse);
    assert(response.header.requestId == 47);

    const auto catalog =
        dnf::DecodeDungeonCatalogResponsePayload(response.payload);
    assert(catalog.result == dnf::CatalogResult::Success);
    assert(catalog.dungeons.size() == 1);
    assert(catalog.dungeons[0].templateId == 1001);
    assert(catalog.dungeons[0].displayName == "Forest");
    assert(catalog.dungeons[0].recommendedPartySize == 1);
    assert(catalog.dungeons[0].maxPartySize == 4);
    assert(catalog.dungeons[0].available);
}

void TestMissingHandler()
{
    TestContext context;
    dnf::Packet request;
    request.header.type = dnf::LoginResponse;

    dnf::PacketDispatcher dispatcher(
        context.channelManager,
        context.partyManager,
        context.dungeonManager,
        context.dungeonUdpManager,
        100);
    bool errorOccurred = false;

    try
    {
        dispatcher.Dispatch(request);
    }
    catch (const std::runtime_error&)
    {
        errorOccurred = true;
    }

    assert(errorOccurred == true);
}

void TestJoinChannelRequest()
{
    TestContext context;
    context.channelManager.AddChannel(1, "Channel 1", 100);

    dnf::Packet request;
    request.header.type = dnf::JoinChannelRequest;
    request.header.requestId = 45;
    request.payload = dnf::EncodeJoinChannelRequestPayload(1);

    dnf::PacketDispatcher dispatcher(
        context.channelManager,
        context.partyManager,
        context.dungeonManager,
        context.dungeonUdpManager,
        700);
    const auto responseBytes = dispatcher.Dispatch(request);

    dnf::ReceiveBuffer buffer;
    buffer.Append(responseBytes);

    dnf::Packet response;
    assert(buffer.TryPop(response) == true);
    assert(response.header.type == dnf::JoinChannelResponse);
    assert(response.header.requestId == 45);

    const auto result =
        dnf::DecodeJoinChannelResponsePayload(response.payload);
    assert(result.result == dnf::JoinChannelResult::Success);
    assert(result.channelId == 1);
    assert(context.channelManager.GetJoinedChannel(700).value() == 1);

    dnf::Packet missingRequest;
    missingRequest.header.type = dnf::JoinChannelRequest;
    missingRequest.header.requestId = 46;
    missingRequest.payload = dnf::EncodeJoinChannelRequestPayload(999);
    buffer.Append(dispatcher.Dispatch(missingRequest));

    dnf::Packet missingResponse;
    assert(buffer.TryPop(missingResponse));
    const auto missingResult =
        dnf::DecodeJoinChannelResponsePayload(missingResponse.payload);
    assert(missingResult.result ==
           dnf::JoinChannelResult::ChannelNotFound);
    assert(missingResult.channelId == 0);

    bool threw = false;
    try
    {
        dnf::DecodeJoinChannelResponsePayload(
            dnf::EncodeJoinChannelRequestPayload(1));
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    assert(threw);

    threw = false;
    try
    {
        dnf::EncodeJoinChannelResponsePayload(
            dnf::JoinChannelResult::Success,
            0);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    assert(threw);
}

void TestInvalidLoginRequest()
{
    TestContext context;
    dnf::Packet request;
    request.header.type = dnf::LoginRequest;
    request.header.requestId = 43;
    request.payload = dnf::EncodeLoginRequestPayload("");

    dnf::PacketDispatcher dispatcher(
        context.channelManager,
        context.partyManager,
        context.dungeonManager,
        context.dungeonUdpManager,
        100);
    const auto responseBytes = dispatcher.Dispatch(request);

    dnf::ReceiveBuffer buffer;
    buffer.Append(responseBytes);

    dnf::Packet response;
    assert(buffer.TryPop(response) == true);
    const auto loginResponse =
        dnf::DecodeLoginResponsePayload(response.payload);
    assert(loginResponse.result == dnf::EmptyPlayerName);
    assert(loginResponse.sessionId == 0);
}

dnf::CreatePartyResponseData SendCreatePartyRequest(
    TestContext& context,
    dnf::SessionId sessionId,
    std::uint32_t requestId = 70)
{
    dnf::Packet request;
    request.header.type = dnf::CreatePartyRequest;
    request.header.requestId = requestId;
    request.payload = dnf::EncodeCreatePartyRequestPayload();

    dnf::PacketDispatcher dispatcher(
        context.channelManager,
        context.partyManager,
        context.dungeonManager,
        context.dungeonUdpManager,
        sessionId);
    const auto responseBytes = dispatcher.Dispatch(request);

    dnf::ReceiveBuffer buffer;
    buffer.Append(responseBytes);

    dnf::Packet response;
    assert(buffer.TryPop(response));
    assert(response.header.type == dnf::CreatePartyResponse);
    assert(response.header.requestId == requestId);
    return dnf::DecodeCreatePartyResponsePayload(response.payload);
}

void TestCreatePartyRequest()
{
    TestContext context;

    const auto response = SendCreatePartyRequest(context, 700);

    assert(response.result == dnf::CreatePartyResult::Success);
    assert(response.partyId != 0);
    assert(response.leaderSessionId == 700);

    const auto party = context.partyManager.GetParty(response.partyId);
    assert(party.has_value());
    assert(party->leaderSessionId == 700);
    assert(party->members.size() == 1);
    assert(party->members[0] == 700);
}

void TestDuplicateCreatePartyRequest()
{
    TestContext context;

    const auto firstResponse = SendCreatePartyRequest(context, 700, 70);
    const auto duplicateResponse =
        SendCreatePartyRequest(context, 700, 71);

    assert(firstResponse.result == dnf::CreatePartyResult::Success);
    assert(duplicateResponse.result ==
           dnf::CreatePartyResult::AlreadyInParty);
    assert(duplicateResponse.partyId == 0);
    assert(duplicateResponse.leaderSessionId == 0);
    assert(context.partyManager.GetJoinedParty(700) ==
           firstResponse.partyId);
}

dnf::JoinPartyResponseData SendJoinPartyRequest(
    TestContext& context,
    dnf::SessionId sessionId,
    dnf::PartyId partyId)
{
    dnf::Packet request;
    request.header.type = dnf::JoinPartyRequest;
    request.header.requestId = 72;
    request.payload = dnf::EncodeJoinPartyRequestPayload(partyId);

    dnf::PacketDispatcher dispatcher(
        context.channelManager,
        context.partyManager,
        context.dungeonManager,
        context.dungeonUdpManager,
        sessionId);

    dnf::ReceiveBuffer buffer;
    buffer.Append(dispatcher.Dispatch(request));

    dnf::Packet response;
    assert(buffer.TryPop(response));
    assert(response.header.type == dnf::JoinPartyResponse);
    assert(response.header.requestId == 72);
    return dnf::DecodeJoinPartyResponsePayload(response.payload);
}

void TestJoinPartyRequest()
{
    TestContext context;
    const dnf::PartyId partyId =
        context.partyManager.CreateParty(700).value();

    const auto response = SendJoinPartyRequest(context, 701, partyId);

    assert(response.result == dnf::JoinPartyResult::Success);
    assert(response.partyId == partyId);
    assert(response.leaderSessionId == 700);

    const auto party = context.partyManager.GetParty(partyId);
    assert(party.has_value());
    assert(party->members.size() == 2);
    assert(party->members[1] == 701);
}

void TestJoinMissingPartyRequest()
{
    TestContext context;

    const auto response = SendJoinPartyRequest(context, 701, 999);

    assert(response.result == dnf::JoinPartyResult::PartyNotFound);
    assert(response.partyId == 0);
    assert(response.leaderSessionId == 0);
}

dnf::LeavePartyResult SendLeavePartyRequest(
    TestContext& context,
    dnf::SessionId sessionId)
{
    dnf::Packet request;
    request.header.type = dnf::LeavePartyRequest;
    request.header.requestId = 73;
    request.payload = dnf::EncodeLeavePartyRequestPayload();

    dnf::PacketDispatcher dispatcher(
        context.channelManager,
        context.partyManager,
        context.dungeonManager,
        context.dungeonUdpManager,
        sessionId);

    dnf::ReceiveBuffer buffer;
    buffer.Append(dispatcher.Dispatch(request));

    dnf::Packet response;
    assert(buffer.TryPop(response));
    assert(response.header.type == dnf::LeavePartyResponse);
    assert(response.header.requestId == 73);
    return dnf::DecodeLeavePartyResponsePayload(response.payload);
}

void TestLeavePartyRequest()
{
    TestContext context;
    const dnf::PartyId partyId =
        context.partyManager.CreateParty(700).value();
    assert(context.partyManager.JoinParty(partyId, 701) ==
           dnf::JoinPartyResult::Success);

    assert(SendLeavePartyRequest(context, 700) ==
           dnf::LeavePartyResult::Success);
    assert(!context.partyManager.GetJoinedParty(700).has_value());

    const auto party = context.partyManager.GetParty(partyId);
    assert(party.has_value());
    assert(party->leaderSessionId == 701);
    assert(party->members.size() == 1);

    assert(SendLeavePartyRequest(context, 700) ==
           dnf::LeavePartyResult::NotInParty);
}

dnf::PartySnapshotData SendPartySnapshotRequest(
    TestContext& context,
    dnf::SessionId sessionId)
{
    dnf::Packet request;
    request.header.type = dnf::PartySnapshotRequest;
    request.header.requestId = 74;
    request.payload = dnf::EncodePartySnapshotRequestPayload();

    dnf::PacketDispatcher dispatcher(
        context.channelManager,
        context.partyManager,
        context.dungeonManager,
        context.dungeonUdpManager,
        sessionId);

    dnf::ReceiveBuffer buffer;
    buffer.Append(dispatcher.Dispatch(request));

    dnf::Packet response;
    assert(buffer.TryPop(response));
    assert(response.header.type == dnf::PartySnapshotResponse);
    assert(response.header.requestId == 74);
    return dnf::DecodePartySnapshotResponsePayload(response.payload);
}

void TestPartySnapshotRequest()
{
    TestContext context;

    const auto noParty = SendPartySnapshotRequest(context, 700);
    assert(noParty.result == dnf::PartySnapshotResult::NotInParty);

    const dnf::PartyId partyId =
        context.partyManager.CreateParty(700).value();
    assert(context.partyManager.JoinParty(partyId, 701) ==
           dnf::JoinPartyResult::Success);

    const auto snapshot = SendPartySnapshotRequest(context, 701);
    assert(snapshot.result == dnf::PartySnapshotResult::Success);
    assert(snapshot.partyId == partyId);
    assert(snapshot.leaderSessionId == 700);
    assert(snapshot.members.size() == 2);
    assert(snapshot.members[0] == 700);
    assert(snapshot.members[1] == 701);

    assert(context.partyManager.LeaveParty(700));
    const auto transferred = SendPartySnapshotRequest(context, 701);
    assert(transferred.leaderSessionId == 701);
    assert(transferred.members.size() == 1);
}

dnf::EnterDungeonResponseData SendEnterDungeonRequest(
    TestContext& context,
    dnf::SessionId sessionId,
    dnf::DungeonTemplateId templateId)
{
    dnf::Packet request;
    request.header.type = dnf::EnterDungeonRequest;
    request.header.requestId = 50;
    request.payload = dnf::EncodeEnterDungeonRequestPayload(templateId);

    dnf::PacketDispatcher dispatcher(
        context.channelManager,
        context.partyManager,
        context.dungeonManager,
        context.dungeonUdpManager,
        sessionId);
    const auto responseBytes = dispatcher.Dispatch(request);

    dnf::ReceiveBuffer buffer;
    buffer.Append(responseBytes);

    dnf::Packet response;
    assert(buffer.TryPop(response));
    assert(response.header.type == dnf::EnterDungeonResponse);
    assert(response.header.requestId == 50);
    return dnf::DecodeEnterDungeonResponsePayload(response.payload);
}

dnf::DungeonConnectionInfoData SendConnectionInfoRequest(
    TestContext& context,
    dnf::SessionId sessionId)
{
    dnf::Packet request;
    request.header.type = dnf::DungeonConnectionInfoRequest;
    request.header.requestId = 61;
    request.payload =
        dnf::EncodeDungeonConnectionInfoRequestPayload();

    dnf::PacketDispatcher dispatcher(
        context.channelManager,
        context.partyManager,
        context.dungeonManager,
        context.dungeonUdpManager,
        sessionId);
    const auto responseBytes = dispatcher.Dispatch(request);

    dnf::ReceiveBuffer buffer;
    buffer.Append(responseBytes);

    dnf::Packet response;
    assert(buffer.TryPop(response));
    assert(response.header.type == dnf::DungeonConnectionInfoResponse);
    assert(response.header.requestId == 61);
    return dnf::DecodeDungeonConnectionInfoResponsePayload(
        response.payload);
}

dnf::DungeonStaticDataResponseData SendDungeonStaticDataRequest(
    TestContext& context,
    dnf::SessionId sessionId,
    dnf::DungeonId dungeonId)
{
    dnf::Packet request;
    request.header.type = dnf::DungeonStaticDataRequest;
    request.header.requestId = 62;
    request.payload =
        dnf::EncodeDungeonStaticDataRequestPayload(dungeonId);

    dnf::PacketDispatcher dispatcher(
        context.channelManager,
        context.partyManager,
        context.dungeonManager,
        context.dungeonUdpManager,
        sessionId);

    dnf::ReceiveBuffer buffer;
    buffer.Append(dispatcher.Dispatch(request));

    dnf::Packet response;
    assert(buffer.TryPop(response));
    assert(response.header.type == dnf::DungeonStaticDataResponse);
    assert(response.header.requestId == 62);
    return dnf::DecodeDungeonStaticDataResponsePayload(response.payload);
}

void TestDungeonStaticDataRequest()
{
    TestContext context;
    const dnf::PartyId partyId =
        context.partyManager.CreateParty(700).value();
    assert(context.partyManager.JoinParty(partyId, 701) ==
           dnf::JoinPartyResult::Success);

    const auto admission = SendEnterDungeonRequest(context, 700, 1001);
    assert(admission.result == dnf::EnterDungeonResult::Success);

    const auto staticData = SendDungeonStaticDataRequest(
        context,
        701,
        admission.dungeonId);
    assert(staticData.result == dnf::DungeonStaticDataResult::Success);
    assert(staticData.dungeonId == admission.dungeonId);
    assert(staticData.dungeonTemplateId == 1001);
    assert(staticData.rooms.size() == 1);
    assert(staticData.rooms[0].width == 1200.0f);
    assert(staticData.rooms[0].obstacles.size() == 1);
    assert(staticData.rooms[0].enemySpawns.size() == 1);
    assert(staticData.enemyTemplates.size() == 1);
    assert(staticData.enemyTemplates[0].name == "Goblin");

    const auto outsider = SendDungeonStaticDataRequest(
        context,
        999,
        admission.dungeonId);
    assert(outsider.result ==
           dnf::DungeonStaticDataResult::NotDungeonParticipant);

    const auto missing = SendDungeonStaticDataRequest(context, 700, 9999);
    assert(missing.result ==
           dnf::DungeonStaticDataResult::DungeonNotFound);
}

void TestPartyLeaderEntersDungeon()
{
    TestContext context;
    const dnf::PartyId partyId =
        context.partyManager.CreateParty(700).value();

    const auto response = SendEnterDungeonRequest(context, 700, 1001);

    assert(response.result == dnf::EnterDungeonResult::Success);
    assert(response.dungeonId != 0);
    assert(response.udpPort != 0);
    assert(response.udpToken != 0);
    assert(context.dungeonUdpManager.FindPort(response.dungeonId) ==
           response.udpPort);
    assert(context.dungeonUdpManager.FindToken(response.dungeonId, 700) ==
           response.udpToken);
    const auto dungeon =
        context.dungeonManager.FindDungeonByParty(partyId);
    assert(dungeon != nullptr);
    assert(dungeon->State() == dnf::DungeonState::Waiting);
}

void TestDungeonEntryPermission()
{
    TestContext context;

    const auto noParty = SendEnterDungeonRequest(context, 700, 1001);
    assert(noParty.result == dnf::EnterDungeonResult::NotInParty);

    const dnf::PartyId partyId =
        context.partyManager.CreateParty(700).value();
    assert(context.partyManager.JoinParty(partyId, 701) ==
           dnf::JoinPartyResult::Success);

    const auto memberRequest =
        SendEnterDungeonRequest(context, 701, 1001);
    assert(memberRequest.result ==
           dnf::EnterDungeonResult::NotPartyLeader);
    assert(context.dungeonManager.ActiveDungeonCount() == 0);
}

void TestDungeonEntryFailures()
{
    TestContext context;
    context.partyManager.CreateParty(700);

    const auto missingTemplate =
        SendEnterDungeonRequest(context, 700, 9999);
    assert(missingTemplate.result ==
           dnf::EnterDungeonResult::DungeonTemplateNotFound);

    const auto firstEntry = SendEnterDungeonRequest(context, 700, 1001);
    assert(firstEntry.result == dnf::EnterDungeonResult::Success);

    const auto duplicateEntry =
        SendEnterDungeonRequest(context, 700, 1001);
    assert(duplicateEntry.result ==
           dnf::EnterDungeonResult::PartyAlreadyInDungeon);
}

void TestUdpAllocationRollback()
{
    TestContext context;
    context.partyManager.CreateParty(700);

    // 다음에 생성될 DungeonId 1을 미리 점유해 할당 실패를 만든다.
    assert(context.dungeonUdpManager.Allocate(1, {999}).has_value());

    const auto response = SendEnterDungeonRequest(context, 700, 1001);
    assert(response.result == dnf::EnterDungeonResult::UdpAllocationFailed);
    assert(response.dungeonId == 0);
    assert(response.udpPort == 0);
    assert(response.udpToken == 0);
    assert(context.dungeonManager.ActiveDungeonCount() == 0);
}

void TestPartyMemberGetsConnectionInfo()
{
    TestContext context;
    const dnf::PartyId partyId =
        context.partyManager.CreateParty(700).value();
    assert(context.partyManager.JoinParty(partyId, 701) ==
           dnf::JoinPartyResult::Success);

    const auto leaderInfo = SendEnterDungeonRequest(context, 700, 1001);
    const auto memberInfo = SendConnectionInfoRequest(context, 701);

    assert(memberInfo.result ==
           dnf::DungeonConnectionInfoResult::Success);
    assert(memberInfo.dungeonId == leaderInfo.dungeonId);
    assert(memberInfo.udpPort == leaderInfo.udpPort);
    assert(memberInfo.udpToken != 0);
    assert(memberInfo.udpToken != leaderInfo.udpToken);
}

void TestConnectionInfoFailures()
{
    TestContext context;

    const auto noParty = SendConnectionInfoRequest(context, 700);
    assert(noParty.result ==
           dnf::DungeonConnectionInfoResult::NotInParty);

    const dnf::PartyId partyId =
        context.partyManager.CreateParty(700).value();
    const auto noDungeon = SendConnectionInfoRequest(context, 700);
    assert(noDungeon.result ==
           dnf::DungeonConnectionInfoResult::DungeonNotFound);

    SendEnterDungeonRequest(context, 700, 1001);
    assert(context.partyManager.JoinParty(partyId, 702) ==
           dnf::JoinPartyResult::Success);

    const auto lateMember = SendConnectionInfoRequest(context, 702);
    assert(lateMember.result ==
           dnf::DungeonConnectionInfoResult::NotDungeonParticipant);

    TestContext udpMissingContext;
    const dnf::PartyId udpMissingPartyId =
        udpMissingContext.partyManager.CreateParty(800).value();
    const auto created = udpMissingContext.dungeonManager.CreateDungeon(
        udpMissingPartyId,
        1001);
    assert(created.status == dnf::CreateDungeonStatus::Success);

    const auto udpNotReady =
        SendConnectionInfoRequest(udpMissingContext, 800);
    assert(udpNotReady.result ==
           dnf::DungeonConnectionInfoResult::UdpNotReady);
}
} // namespace

int main()
{
    TestLoginRequest();
    TestInvalidLoginRequest();
    TestChannelListRequest();
    TestDungeonCatalogRequest();
    TestJoinChannelRequest();
    TestCreatePartyRequest();
    TestDuplicateCreatePartyRequest();
    TestJoinPartyRequest();
    TestJoinMissingPartyRequest();
    TestLeavePartyRequest();
    TestPartySnapshotRequest();
    TestPartyLeaderEntersDungeon();
    TestDungeonStaticDataRequest();
    TestDungeonEntryPermission();
    TestDungeonEntryFailures();
    TestUdpAllocationRollback();
    TestPartyMemberGetsConnectionInfo();
    TestConnectionInfoFailures();
    TestMissingHandler();

    std::cout << "All packet dispatcher tests passed.\n";
    return 0;
}
