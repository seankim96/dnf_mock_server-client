#include "DevAuthTicketVerifier.h"
#include "PlayerLoginService.h"
#include "SqliteDatabase.h"
#include "SqlitePlayerRepository.h"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include <cassert>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
dnf::PlayerLoginResult WaitForLogin(
    dnf::PlayerLoginService& loginService,
    boost::asio::io_context& ioContext,
    const std::string& ticket,
    std::thread::id* callbackThreadId = nullptr)
{
    auto workGuard = boost::asio::make_work_guard(ioContext);
    std::optional<dnf::PlayerLoginResult> output;

    loginService.Login(
        ticket,
        [&](dnf::PlayerLoginResult result)
        {
            if (callbackThreadId != nullptr)
            {
                *callbackThreadId = std::this_thread::get_id();
            }

            output = std::move(result);
            workGuard.reset();
        });

    ioContext.run();
    ioContext.restart();
    assert(output.has_value());
    return std::move(output.value());
}

void TestSuccessfulLoginLoadsPlayerOnDatabaseWorker()
{
    boost::asio::io_context ioContext;
    dnf::DatabaseExecutor databaseExecutor(1);
    dnf::SqliteDatabase database(":memory:");
    dnf::SqlitePlayerRepository repository(database);
    const dnf::PlayerProfile created =
        repository.CreatePlayer("Player_1").value();

    dnf::DevAuthTicketVerifier ticketVerifier;
    assert(ticketVerifier.RegisterTicket(
        "valid-ticket",
        {10, created.playerId}));

    dnf::PlayerLoginService loginService(
        ioContext,
        databaseExecutor,
        ticketVerifier,
        repository);

    const std::thread::id ioThreadId = std::this_thread::get_id();
    std::thread::id callbackThreadId;
    const dnf::PlayerLoginResult result = WaitForLogin(
        loginService,
        ioContext,
        "valid-ticket",
        &callbackThreadId);

    assert(result.status == dnf::PlayerLoginStatus::Success);
    assert(result.authContext.has_value());
    assert(result.authContext->accountId == 10);
    assert(result.profile.has_value());
    assert(result.profile->playerId == created.playerId);
    assert(result.profile->name == "Player_1");
    assert(callbackThreadId == ioThreadId);
    assert(!ticketVerifier.Verify("valid-ticket").has_value());
}

void TestInvalidTicketCompletesAsynchronously()
{
    boost::asio::io_context ioContext;
    dnf::DatabaseExecutor databaseExecutor(1);
    dnf::SqliteDatabase database(":memory:");
    dnf::SqlitePlayerRepository repository(database);
    dnf::DevAuthTicketVerifier ticketVerifier;
    dnf::PlayerLoginService loginService(
        ioContext,
        databaseExecutor,
        ticketVerifier,
        repository);

    bool callbackCalled = false;
    loginService.Login(
        "missing-ticket",
        [&](dnf::PlayerLoginResult result)
        {
            callbackCalled = true;
            assert(result.status == dnf::PlayerLoginStatus::InvalidTicket);
            assert(!result.authContext.has_value());
            assert(!result.profile.has_value());
        });

    assert(!callbackCalled);
    ioContext.run();
    assert(callbackCalled);
}

void TestMissingPlayerIsReported()
{
    boost::asio::io_context ioContext;
    dnf::DatabaseExecutor databaseExecutor(1);
    dnf::SqliteDatabase database(":memory:");
    dnf::SqlitePlayerRepository repository(database);
    dnf::DevAuthTicketVerifier ticketVerifier;
    assert(ticketVerifier.RegisterTicket("missing-player", {10, 9999}));

    dnf::PlayerLoginService loginService(
        ioContext,
        databaseExecutor,
        ticketVerifier,
        repository);
    const dnf::PlayerLoginResult result = WaitForLogin(
        loginService,
        ioContext,
        "missing-player");

    assert(result.status == dnf::PlayerLoginStatus::PlayerNotFound);
    assert(result.authContext.has_value());
    assert(!result.profile.has_value());
}

class FailingPlayerRepository final : public dnf::PlayerRepository
{
public:
    std::optional<dnf::PlayerProfile> FindPlayer(dnf::PlayerId) override
    {
        throw std::runtime_error("storage unavailable");
    }

    std::optional<dnf::PlayerProfile> FindPlayerByName(
        const std::string&) override
    {
        return std::nullopt;
    }

    std::optional<dnf::PlayerProfile> CreatePlayer(
        const std::string&) override
    {
        return std::nullopt;
    }

    bool SavePlayer(const dnf::PlayerProfile&) override
    {
        return false;
    }
};

void TestStorageFailureIsReturnedToIoThread()
{
    boost::asio::io_context ioContext;
    dnf::DatabaseExecutor databaseExecutor(1);
    FailingPlayerRepository repository;
    dnf::DevAuthTicketVerifier ticketVerifier;
    assert(ticketVerifier.RegisterTicket("db-error", {10, 100}));

    dnf::PlayerLoginService loginService(
        ioContext,
        databaseExecutor,
        ticketVerifier,
        repository);
    const dnf::PlayerLoginResult result = WaitForLogin(
        loginService,
        ioContext,
        "db-error");

    assert(result.status == dnf::PlayerLoginStatus::StorageError);
    assert(result.authContext.has_value());
    assert(!result.profile.has_value());
}
} // namespace

int main()
{
    TestSuccessfulLoginLoadsPlayerOnDatabaseWorker();
    TestInvalidTicketCompletesAsynchronously();
    TestMissingPlayerIsReported();
    TestStorageFailureIsReturnedToIoThread();

    std::cout << "All player login service tests passed.\n";
    return 0;
}
