#include "ChannelProtocol.h"
#include "DungeonAdmissionProtocol.h"
#include "DungeonConnectionProtocol.h"
#include "DungeonManager.h"
#include "DungeonUdpManager.h"
#include "EnemyCatalog.h"
#include "PacketDispatcher.h"
#include "PartyManager.h"
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
        const dnf::RoomTemplate room{
            1, 1200.0f, 500.0f, {100.0f, 250.0f, 0.0f}};
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
    request.payload = {'M', 'o', 'c', 'k'};

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
    assert(response.payload == std::vector<std::uint8_t>({0}));
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

    const auto channels = dnf::DecodeChannelListPayload(response.payload);
    assert(channels.size() == 2);
    assert(channels[0].id == 1);
    assert(channels[0].currentPlayers == 1);
    assert(channels[0].maxPlayers == 100);
    assert(channels[1].id == 2);
    assert(channels[1].currentPlayers == 0);
    assert(channels[1].maxPlayers == 200);
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
}

void TestInvalidLoginRequest()
{
    TestContext context;
    dnf::Packet request;
    request.header.type = dnf::LoginRequest;
    request.header.requestId = 43;
    request.payload = {};

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
    assert(response.payload == std::vector<std::uint8_t>({1}));
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
    TestJoinChannelRequest();
    TestPartyLeaderEntersDungeon();
    TestDungeonEntryPermission();
    TestDungeonEntryFailures();
    TestUdpAllocationRollback();
    TestPartyMemberGetsConnectionInfo();
    TestConnectionInfoFailures();
    TestMissingHandler();

    std::cout << "All packet dispatcher tests passed.\n";
    return 0;
}
