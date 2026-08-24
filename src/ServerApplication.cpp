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
constexpr std::size_t DATABASE_THREAD_COUNT = 2;
}

ServerApplication::ServerApplication(
    std::uint16_t port,
    const std::string& databasePath,
    const std::string& dataDirectory)
    : database_(databasePath),
      playerRepository_(database_),
      authTicketStore_(database_),
      authTicketVerifier_(authTicketStore_),
      databaseExecutor_(DATABASE_THREAD_COUNT),
      playerLoginService_(
          ioContext_,
          databaseExecutor_,
          authTicketVerifier_,
          playerRepository_),
      dungeonUdpManager_(ioContext_),
      dungeonCatalog_(enemyCatalog_),
      dungeonManager_(partyManager_, dungeonCatalog_, enemyCatalog_),
      dungeonTickService_(
          ioContext_,
          dungeonManager_,
          dungeonUdpManager_,
          skillCatalog_),
      sessionManager_(
          channelManager_,
          partyManager_,
          dungeonManager_,
          dungeonUdpManager_,
          playerLoginService_),
      tcpServer_(ioContext_, port, sessionManager_)
{
    LoadGameData(dataDirectory);
}

void ServerApplication::LoadGameData(const std::string& dataDirectory)
{
    GameDataLoader loader(
        channelManager_,
        skillCatalog_,
        enemyCatalog_,
        dungeonCatalog_);
    gameData_ = loader.Load(dataDirectory);
}

void ServerApplication::Run()
{
    dungeonTickService_.Start();
    tcpServer_.Start();

    std::cout << "Boost.Asio TCP server started"
              << " ioThreads=" << IO_THREAD_COUNT
              << " databaseThreads=" << DATABASE_THREAD_COUNT << '\n';

    for (const ChannelInfo& channel : channelManager_.GetChannelList())
    {
        std::cout << "Channel ready"
                  << " id=" << channel.id
                  << " name=" << channel.name
                  << " capacity=" << channel.maxPlayers << '\n';
    }

    std::cout << "Game data ready"
              << " version=" << gameData_.contentVersion
              << " channels=" << gameData_.channelCount
              << " skills=" << gameData_.skillCount
              << " enemies=" << gameData_.enemyCount
              << " dungeons=" << gameData_.dungeonCount << '\n';

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

const SkillCatalog& ServerApplication::Skills() const
{
    return skillCatalog_;
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
