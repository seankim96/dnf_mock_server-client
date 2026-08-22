#include "DungeonTickService.h"
#include "DungeonProtocol.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/steady_timer.hpp>

#include <cassert>
#include <chrono>
#include <iostream>

namespace
{
void TestTickTimerRunsAndStops()
{
    boost::asio::io_context ioContext;
    dnf::DungeonUdpManager udpManager(ioContext);
    dnf::PartyManager partyManager;
    dnf::EnemyCatalog enemyCatalog;
    dnf::SkillCatalog skillCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);
    dnf::DungeonManager dungeonManager(
        partyManager,
        dungeonCatalog,
        enemyCatalog);

    dnf::DungeonTickService tickService(
        ioContext,
        dungeonManager,
        udpManager,
        skillCatalog);

    tickService.Start();
    tickService.Start();
    assert(tickService.IsRunning());

    boost::asio::steady_timer stopTimer(
        ioContext,
        std::chrono::milliseconds(120));
    stopTimer.async_wait(
        [&tickService](const boost::system::error_code&)
        {
            tickService.Stop();
        });

    ioContext.run();

    assert(!tickService.IsRunning());
    assert(tickService.TickCount() >= 2);
}

void TestDungeonStartsAfterAllUdpHelloMessages()
{
    using boost::asio::ip::udp;

    boost::asio::io_context ioContext;
    dnf::DungeonUdpManager udpManager(ioContext);
    dnf::PartyManager partyManager;
    const dnf::PartyId partyId = partyManager.CreateParty(100).value();
    assert(partyManager.JoinParty(partyId, 200) ==
           dnf::JoinPartyResult::Success);

    dnf::EnemyCatalog enemyCatalog;
    dnf::EnemyTemplate enemy;
    enemy.id = 2001;
    enemy.name = "Goblin";
    enemy.maxHp = 100;
    enemy.collision = {
        {-20.0f, -15.0f, 0.0f},
        {20.0f, 15.0f, 80.0f}};
    assert(enemyCatalog.AddEnemy(enemy));

    dnf::SkillCatalog skillCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);
    dnf::RoomTemplate room{
        1, 1200.0f, 500.0f, {100.0f, 250.0f, 0.0f}};
    room.enemySpawns.push_back(
        {1, enemy.id, {500.0f, 250.0f, 0.0f}, 1});
    assert(dungeonCatalog.AddDungeon(1001, "Forest", {room}));

    dnf::DungeonManager dungeonManager(
        partyManager,
        dungeonCatalog,
        enemyCatalog);
    const auto created = dungeonManager.CreateDungeon(partyId, 1001);
    const dnf::DungeonId dungeonId = created.dungeon->Id();

    const auto port = udpManager.Allocate(dungeonId, {100, 200});
    const auto firstToken = udpManager.FindToken(dungeonId, 100);
    const auto secondToken = udpManager.FindToken(dungeonId, 200);
    assert(port.has_value());
    assert(firstToken.has_value());
    assert(secondToken.has_value());

    boost::asio::io_context clientIoContext;
    udp::socket firstClient(clientIoContext, udp::endpoint(udp::v4(), 0));
    udp::socket secondClient(clientIoContext, udp::endpoint(udp::v4(), 0));
    const udp::endpoint serverEndpoint(
        boost::asio::ip::address_v4::loopback(),
        port.value());

    const auto firstHello = dnf::EncodeUdpHello(
        {dungeonId, 100, firstToken.value()});
    const auto secondHello = dnf::EncodeUdpHello(
        {dungeonId, 200, secondToken.value()});
    firstClient.send_to(boost::asio::buffer(firstHello), serverEndpoint);

    dnf::DungeonTickService tickService(
        ioContext,
        dungeonManager,
        udpManager,
        skillCatalog);
    tickService.Start();

    bool stayedWaitingForFirstPlayer = false;
    boost::asio::steady_timer secondHelloTimer(
        ioContext,
        std::chrono::milliseconds(80));
    secondHelloTimer.async_wait(
        [&](const boost::system::error_code&)
        {
            stayedWaitingForFirstPlayer =
                created.dungeon->State() == dnf::DungeonState::Waiting;
            secondClient.send_to(
                boost::asio::buffer(secondHello),
                serverEndpoint);
        });

    boost::asio::steady_timer stopTimer(
        ioContext,
        std::chrono::milliseconds(180));
    stopTimer.async_wait(
        [&](const boost::system::error_code&)
        {
            tickService.Stop();
            udpManager.Release(dungeonId);
        });

    ioContext.run();

    assert(stayedWaitingForFirstPlayer);
    assert(created.dungeon->State() == dnf::DungeonState::Running);
}

void TestWaitingDungeonTimesOut()
{
    boost::asio::io_context ioContext;
    dnf::DungeonUdpManager udpManager(ioContext);
    dnf::PartyManager partyManager;
    const dnf::PartyId partyId = partyManager.CreateParty(100).value();

    dnf::EnemyCatalog enemyCatalog;
    dnf::SkillCatalog skillCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);
    const dnf::RoomTemplate room{
        1, 1200.0f, 500.0f, {100.0f, 250.0f, 0.0f}};
    assert(dungeonCatalog.AddDungeon(1001, "Forest", {room}));

    dnf::DungeonManager dungeonManager(
        partyManager,
        dungeonCatalog,
        enemyCatalog);
    const auto created = dungeonManager.CreateDungeon(partyId, 1001);
    const dnf::DungeonId dungeonId = created.dungeon->Id();
    assert(udpManager.Allocate(dungeonId, {100}).has_value());

    dnf::DungeonTickService tickService(
        ioContext,
        dungeonManager,
        udpManager,
        skillCatalog,
        std::chrono::milliseconds(90));
    tickService.Start();

    boost::asio::steady_timer stopTimer(
        ioContext,
        std::chrono::milliseconds(250));
    stopTimer.async_wait(
        [&](const boost::system::error_code&)
        {
            tickService.Stop();

            if (udpManager.FindPort(dungeonId).has_value())
            {
                udpManager.Release(dungeonId);
            }
        });

    ioContext.run();

    assert(dungeonManager.FindDungeon(dungeonId) == nullptr);
    assert(udpManager.AllocationCount() == 0);

    const auto retry = dungeonManager.CreateDungeon(partyId, 1001);
    assert(retry.status == dnf::CreateDungeonStatus::Success);
}
} // namespace

int main()
{
    TestTickTimerRunsAndStops();
    TestDungeonStartsAfterAllUdpHelloMessages();
    TestWaitingDungeonTimesOut();

    std::cout << "All dungeon tick service tests passed.\n";
    return 0;
}
