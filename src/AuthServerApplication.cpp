#include "AuthServerApplication.h"

#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>
#include <boost/system/error_code.hpp>

#include <csignal>
#include <exception>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

namespace dnf
{
namespace
{
constexpr std::size_t AUTH_IO_THREAD_COUNT = 2;
constexpr std::size_t AUTH_DATABASE_THREAD_COUNT = 1;
}

AuthServerApplication::AuthServerApplication(
    std::uint16_t port,
    const std::string& databasePath,
    const std::string& certificateChainPath,
    const std::string& privateKeyPath,
    GameServerAddress gameServerAddress)
    : shutdownSignals_(ioContext_, SIGINT, SIGTERM),
      database_(databasePath),
      accountRepository_(database_),
      playerRepository_(database_),
      accountPlayerRepository_(database_),
      authTicketStore_(database_),
      authTicketIssuer_(accountPlayerRepository_, authTicketStore_),
      databaseExecutor_(AUTH_DATABASE_THREAD_COUNT),
      authenticationService_(
          ioContext_,
          databaseExecutor_,
          accountRepository_,
          passwordHasher_),
      characterListService_(
          ioContext_,
          databaseExecutor_,
          accountRepository_,
          accountPlayerRepository_,
          playerRepository_),
      characterSelectionService_(
          ioContext_,
          databaseExecutor_,
          authTicketIssuer_),
      tlsServer_(
          ioContext_,
          port,
          certificateChainPath,
          privateKeyPath,
          authenticationService_,
          characterListService_,
          characterSelectionService_,
          std::move(gameServerAddress))
{
}

void AuthServerApplication::Start()
{
    tlsServer_.Start();
    WaitForShutdownSignal();

    std::cout << "Authentication server started"
              << " port=" << tlsServer_.Port()
              << " ioThreads=" << AUTH_IO_THREAD_COUNT
              << " databaseThreads=" << AUTH_DATABASE_THREAD_COUNT
              << '\n';
}

void AuthServerApplication::WaitForShutdownSignal()
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
                std::cout << "Authentication server stopping"
                          << " signal=" << signalNumber << '\n';
            }

            StopOnIoContext();
        });
}

void AuthServerApplication::Run()
{
    std::vector<std::thread> ioThreads;
    ioThreads.reserve(AUTH_IO_THREAD_COUNT);

    for (std::size_t index = 0;
         index < AUTH_IO_THREAD_COUNT;
         ++index)
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
                    std::cerr << "Authentication I/O worker error: "
                              << exception.what() << '\n';
                    ioContext_.stop();
                }
            });
    }

    for (std::thread& thread : ioThreads)
    {
        thread.join();
    }
}

void AuthServerApplication::Stop()
{
    boost::asio::post(
        ioContext_,
        [this]
        {
            StopOnIoContext();
        });
}

void AuthServerApplication::StopOnIoContext()
{
    boost::system::error_code ignoredError;
    shutdownSignals_.cancel(ignoredError);
    tlsServer_.Stop();
    ioContext_.stop();
}

std::uint16_t AuthServerApplication::Port() const
{
    return tlsServer_.Port();
}
} // namespace dnf
