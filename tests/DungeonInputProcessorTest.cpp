#include "DungeonInputProcessor.h"
#include "DungeonProtocol.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/udp.hpp>

#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace
{
bool IsNear(float value, float expected)
{
    return std::abs(value - expected) < 0.001f;
}

void WaitForInputCount(
    const dnf::DungeonUdpManager& udpManager,
    dnf::DungeonId dungeonId,
    std::size_t expectedCount)
{
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (udpManager.PendingInputCount(dungeonId) == expectedCount)
        {
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void TestInputMovesDungeonPlayer()
{
    using boost::asio::ip::udp;

    dnf::PartyManager partyManager;
    const dnf::PartyId partyId = partyManager.CreateParty(100).value();

    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);

    dnf::RoomTemplate room;
    room.id = 1;
    room.width = 1200.0f;
    room.depth = 500.0f;
    room.playerSpawn = {100.0f, 250.0f, 0.0f};

    dnf::ObstacleTemplate obstacle;
    obstacle.id = 1;
    obstacle.collision = {
        {120.0f, 270.0f, 0.0f},
        {140.0f, 290.0f, 100.0f}};
    room.obstacles.push_back(obstacle);

    dnf::RoomTemplate secondRoom;
    secondRoom.id = 2;
    secondRoom.width = 1200.0f;
    secondRoom.depth = 500.0f;
    secondRoom.playerSpawn = {50.0f, 50.0f, 0.0f};

    dnf::PortalTemplate portal;
    portal.id = 1;
    portal.triggerArea = {
        {90.0f, 300.0f, 0.0f},
        {110.0f, 320.0f, 100.0f}};
    portal.targetRoomId = 2;
    portal.targetPosition = secondRoom.playerSpawn;
    room.portals.push_back(portal);

    assert(dungeonCatalog.AddDungeon(
        1001,
        "Forest",
        {room, secondRoom}));

    dnf::DungeonManager dungeonManager(
        partyManager,
        dungeonCatalog,
        enemyCatalog);
    const auto created = dungeonManager.CreateDungeon(partyId, 1001);
    const dnf::DungeonId dungeonId = created.dungeon->Id();
    assert(dungeonManager.StartDungeon(dungeonId));

    boost::asio::io_context serverIoContext;
    dnf::DungeonUdpManager udpManager(serverIoContext);
    const auto port = udpManager.Allocate(dungeonId, {100});
    const auto token = udpManager.FindToken(dungeonId, 100);
    assert(port.has_value());
    assert(token.has_value());

    std::thread serverThread(
        [&serverIoContext]
        {
            serverIoContext.run();
        });

    boost::asio::io_context clientIoContext;
    udp::socket client(clientIoContext, udp::endpoint(udp::v4(), 0));
    const udp::endpoint serverEndpoint(
        boost::asio::ip::address_v4::loopback(),
        port.value());

    const auto hello = dnf::EncodeUdpHello(
        {dungeonId, 100, token.value()});
    client.send_to(boost::asio::buffer(hello), serverEndpoint);

    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (udpManager.FindEndpoint(dungeonId, 100).has_value())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    dnf::PlayerInputMessage input;
    input.dungeonId = dungeonId;
    input.sequence = 1;
    input.moveX = 1.0f;
    const auto firstInput = dnf::EncodePlayerInput(input);

    input.sequence = 2;
    input.moveX = 0.0f;
    input.moveY = 1.0f;
    const auto latestInput = dnf::EncodePlayerInput(input);

    client.send_to(boost::asio::buffer(firstInput), serverEndpoint);
    client.send_to(boost::asio::buffer(latestInput), serverEndpoint);
    WaitForInputCount(udpManager, dungeonId, 2);

    dnf::DungeonInputProcessor processor(dungeonManager, udpManager);
    const dnf::InputProcessResult moved =
        processor.Process(dungeonId, 0.1f);

    const auto player = created.dungeon->FindPlayer(100);
    const dnf::Position movedPosition = player->CurrentPosition();

    assert(moved.receivedCount == 2);
    assert(moved.appliedCount == 1);
    assert(moved.rejectedCount == 0);
    assert(IsNear(movedPosition.x, 100.0f));
    assert(IsNear(movedPosition.y, 280.0f));

    input.sequence = 3;
    input.moveX = 1.0f;
    input.moveY = 0.0f;
    const auto blockedInput = dnf::EncodePlayerInput(input);
    client.send_to(boost::asio::buffer(blockedInput), serverEndpoint);
    WaitForInputCount(udpManager, dungeonId, 1);

    const dnf::InputProcessResult blocked =
        processor.Process(dungeonId, 0.1f);
    const dnf::Position blockedPosition = player->CurrentPosition();

    assert(blocked.receivedCount == 1);
    assert(blocked.appliedCount == 0);
    assert(blocked.rejectedCount == 1);
    assert(IsNear(blockedPosition.x, 100.0f));
    assert(IsNear(blockedPosition.y, 280.0f));

    input.sequence = 4;
    input.moveX = 0.0f;
    input.moveY = 1.0f;
    const auto portalInput = dnf::EncodePlayerInput(input);
    client.send_to(boost::asio::buffer(portalInput), serverEndpoint);
    WaitForInputCount(udpManager, dungeonId, 1);

    const dnf::InputProcessResult enteredPortal =
        processor.Process(dungeonId, 0.1f);
    const dnf::DungeonPlayerSnapshot nextRoom = player->Snapshot();

    assert(enteredPortal.appliedCount == 1);
    assert(nextRoom.roomId == 2);
    assert(IsNear(nextRoom.position.x, 50.0f));
    assert(IsNear(nextRoom.position.y, 50.0f));

    udpManager.Release(dungeonId);
    serverThread.join();
}
} // namespace

int main()
{
    TestInputMovesDungeonPlayer();

    std::cout << "All dungeon input processor tests passed.\n";
    return 0;
}
