#include "ServerApplication.h"

#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>
#include <boost/system/error_code.hpp>

#include <csignal>
#include <exception>
#include <iostream>
#include <mutex>
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
    : lifecycleStrand_(boost::asio::make_strand(ioContext_)),
      shutdownSignals_(lifecycleStrand_, SIGINT, SIGTERM),
      database_(databasePath),
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
    bool expected = false;
    if (!runStarted_.compare_exchange_strong(expected, true))
    {
        throw std::logic_error(
            "Server application can only run once");
    }

    if (stopRequested_.load())
    {
        databaseExecutor_.DrainAndStop();
        return;
    }

    try
    {
        WaitForShutdownSignal();
        dungeonTickService_.Start();
        tcpServer_.Start();
    }
    catch (...)
    {
        stopRequested_.store(true);
        StopOnIoContext();
        ioContext_.run();
        databaseExecutor_.DrainAndStop();
        throw;
    }

    std::cout << "Boost.Asio TCP server started"
              << " port=" << tcpServer_.Port()
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
    std::exception_ptr workerException;
    std::mutex workerExceptionMutex;

    for (std::size_t index = 0; index < IO_THREAD_COUNT; ++index)
    {
        ioThreads.emplace_back(
            [this, &workerException, &workerExceptionMutex]
            {
                try
                {
                    ioContext_.run();
                }
                catch (const std::exception& exception)
                {
                    std::cerr << "I/O worker error: "
                              << exception.what() << '\n';
                    {
                        std::lock_guard lock(workerExceptionMutex);
                        if (!workerException)
                        {
                            workerException = std::current_exception();
                        }
                    }

                    stopRequested_.store(true);
                    RequestStopOnIoContext();
                }
            });
    }

    for (std::thread& thread : ioThreads)
    {
        thread.join();
    }

    databaseExecutor_.DrainAndStop();

    if (workerException)
    {
        std::rethrow_exception(workerException);
    }
}

void ServerApplication::WaitForShutdownSignal()
{
    shutdownSignals_.async_wait(
        [this](
            const boost::system::error_code& error,
            int signalNumber)
        {
            if (error == boost::asio::error::operation_aborted)
            {
                return;
            }

            if (error)
            {
                std::cerr << "Shutdown signal error: "
                          << error.message() << '\n';
            }
            else
            {
                std::cout << "Game server stopping"
                          << " signal=" << signalNumber << '\n';
            }

            stopRequested_.store(true);
            StopOnIoContext();
        });
}

void ServerApplication::Stop()
{
    if (stopRequested_.exchange(true) || !runStarted_.load())
    {
        return;
    }

    RequestStopOnIoContext();
}

void ServerApplication::RequestStopOnIoContext()
{
    boost::asio::post(
        lifecycleStrand_,
        [this]
        {
            StopOnIoContext();
        });
}

void ServerApplication::StopOnIoContext()
{
    if (shutdownStarted_.exchange(true))
    {
        return;
    }

    boost::system::error_code ignoredError;
    shutdownSignals_.cancel(ignoredError);
    tcpServer_.Stop();
    dungeonTickService_.Stop();
    dungeonManager_.Stop();
    dungeonUdpManager_.Stop();
    sessionManager_.Stop();
}

std::uint16_t ServerApplication::Port() const
{
    return tcpServer_.Port();
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
