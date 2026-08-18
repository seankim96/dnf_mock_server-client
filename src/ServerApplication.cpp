#include "ServerApplication.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace dnf
{
namespace
{
constexpr std::size_t IO_THREAD_COUNT = 4;
}

ServerApplication::ServerApplication(std::uint16_t port)
    : dungeonUdpManager_(ioContext_),
      dungeonCatalog_(enemyCatalog_),
      dungeonManager_(partyManager_, dungeonCatalog_, enemyCatalog_),
      sessionManager_(channelManager_, partyManager_, dungeonManager_),
      tcpServer_(ioContext_, port, sessionManager_)
{
    channelManager_.AddChannel(1, "Channel 1", 100);
    channelManager_.AddChannel(2, "Channel 2", 100);
    channelManager_.AddChannel(3, "Channel 3", 100);

    LoadGameData();
}

void ServerApplication::LoadGameData()
{
    EnemyTemplate goblin;
    goblin.id = 2001;
    goblin.name = "Goblin";
    goblin.maxHp = 100;
    goblin.moveSpeed = 120.0f;
    goblin.aiType = EnemyAiType::Melee;
    goblin.collision = {
        {-20.0f, -15.0f, 0.0f},
        {20.0f, 15.0f, 80.0f}};

    if (!enemyCatalog_.AddEnemy(goblin))
    {
        throw std::runtime_error("Failed to load enemy catalog");
    }

    RoomTemplate firstRoom{
        1, 1200.0f, 500.0f, {100.0f, 250.0f, 0.0f}};
    RoomTemplate secondRoom{
        2, 1500.0f, 600.0f, {100.0f, 300.0f, 0.0f}};

    PortalTemplate portal;
    portal.id = 1;
    portal.triggerArea = {
        {1100.0f, 200.0f, 0.0f},
        {1200.0f, 300.0f, 200.0f}};
    portal.targetRoomId = secondRoom.id;
    portal.targetPosition = secondRoom.playerSpawn;
    firstRoom.portals.push_back(portal);

    ObstacleTemplate crate;
    crate.id = 1;
    crate.collision = {
        {500.0f, 100.0f, 0.0f},
        {580.0f, 180.0f, 100.0f}};
    crate.destructible = true;
    crate.maxHp = 100;
    firstRoom.obstacles.push_back(crate);

    EnemySpawnTemplate enemySpawn;
    enemySpawn.id = 1;
    enemySpawn.enemyTemplateId = goblin.id;
    enemySpawn.position = {800.0f, 250.0f, 0.0f};
    enemySpawn.wave = 1;
    firstRoom.enemySpawns.push_back(enemySpawn);

    if (!dungeonCatalog_.AddDungeon(
            1001,
            "Forest",
            {firstRoom, secondRoom}))
    {
        throw std::runtime_error("Failed to load dungeon catalog");
    }
}

void ServerApplication::Run()
{
    tcpServer_.Start();

    std::cout << "Boost.Asio TCP server started"
              << " ioThreads=" << IO_THREAD_COUNT << '\n';

    for (const ChannelInfo& channel : channelManager_.GetChannelList())
    {
        std::cout << "Channel ready"
                  << " id=" << channel.id
                  << " name=" << channel.name
                  << " capacity=" << channel.maxPlayers << '\n';
    }

    std::cout << "Game data ready"
              << " enemy=2001"
              << " dungeon=1001" << '\n';

    std::vector<std::thread> ioThreads;
    ioThreads.reserve(IO_THREAD_COUNT);

    for (std::size_t index = 0; index < IO_THREAD_COUNT; ++index)
    {
        ioThreads.emplace_back(
            [this]
            {
                try
                {
                    ioContext_.run();
                }
                catch (const std::exception& exception)
                {
                    std::cerr << "I/O worker error: "
                              << exception.what() << '\n';
                }
            });
    }

    for (std::thread& thread : ioThreads)
    {
        thread.join();
    }
}

const EnemyCatalog& ServerApplication::Enemies() const
{
    return enemyCatalog_;
}

const DungeonCatalog& ServerApplication::DungeonTemplates() const
{
    return dungeonCatalog_;
}

const DungeonManager& ServerApplication::DungeonInstances() const
{
    return dungeonManager_;
}

const DungeonUdpManager& ServerApplication::DungeonUdpSockets() const
{
    return dungeonUdpManager_;
}
} // namespace dnf
