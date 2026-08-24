#include "AccountAuthenticationService.h"
#include "AccountPlayerRepository.h"
#include "AuthTicketIssuer.h"
#include "AuthTicketVerifier.h"
#include "CharacterListService.h"
#include "CharacterSelectionService.h"
#include "PlayerLoginService.h"
#include "SqliteAuthTicketStore.h"
#include "SqliteDatabase.h"

#include <boost/asio/io_context.hpp>

#include <cassert>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace
{
class SaturatedDatabaseExecutor
{
public:
    SaturatedDatabaseExecutor()
        : releaseFuture_(release_.get_future().share()),
          executor(1, 1)
    {
        std::promise<void> started;
        std::future<void> startedFuture = started.get_future();
        assert(executor.TryPost(
            [&started, releaseFuture = releaseFuture_]
            {
                started.set_value();
                releaseFuture.wait();
            }));
        startedFuture.wait();
        assert(executor.TryPost(
            [releaseFuture = releaseFuture_]
            {
                releaseFuture.wait();
            }));
    }

    ~SaturatedDatabaseExecutor()
    {
        release_.set_value();
        executor.DrainAndStop();
    }

    SaturatedDatabaseExecutor(const SaturatedDatabaseExecutor&) = delete;
    SaturatedDatabaseExecutor& operator=(
        const SaturatedDatabaseExecutor&) = delete;

private:
    std::promise<void> release_;
    std::shared_future<void> releaseFuture_;

public:
    dnf::DatabaseExecutor executor;
};

class TestPasswordHasher final : public dnf::PasswordHasher
{
public:
    std::optional<std::string> Hash(
        const std::string& password) override
    {
        return "hash:" + password;
    }

    bool Verify(
        const std::string&,
        const std::string&) override
    {
        return false;
    }
};

class EmptyAccountRepository final : public dnf::AccountRepository
{
public:
    std::optional<dnf::Account> FindAccount(
        dnf::AccountId) override
    {
        return std::nullopt;
    }

    std::optional<dnf::Account> FindAccountByLoginId(
        const std::string&) override
    {
        return std::nullopt;
    }

    std::optional<dnf::Account> CreateAccount(
        const std::string&,
        const std::string&) override
    {
        return std::nullopt;
    }
};

class EmptyAccountPlayerRepository final
    : public dnf::AccountPlayerRepository
{
public:
    bool LinkPlayer(dnf::AccountId, dnf::PlayerId) override
    {
        return false;
    }

    bool OwnsPlayer(dnf::AccountId, dnf::PlayerId) override
    {
        return false;
    }

    std::vector<dnf::PlayerId> FindPlayerIds(
        dnf::AccountId) override
    {
        return {};
    }
};

class EmptyPlayerRepository final : public dnf::PlayerRepository
{
public:
    std::optional<dnf::PlayerProfile> FindPlayer(
        dnf::PlayerId) override
    {
        return std::nullopt;
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

class EmptyTicketVerifier final : public dnf::AuthTicketVerifier
{
public:
    std::optional<dnf::AuthContext> Verify(
        const std::string&) override
    {
        return std::nullopt;
    }
};

void TestAuthenticationReturnsServiceBusy()
{
    boost::asio::io_context ioContext;
    SaturatedDatabaseExecutor saturated;
    EmptyAccountRepository accounts;
    TestPasswordHasher passwordHasher;
    dnf::AccountAuthenticationService service(
        ioContext,
        saturated.executor,
        accounts,
        passwordHasher);
    std::optional<dnf::AccountAuthenticationResult> result;

    service.Authenticate(
        "127.0.0.1",
        "account_1",
        "password",
        [&result](dnf::AccountAuthenticationResult value)
        {
            result = std::move(value);
        });

    assert(!result.has_value());
    ioContext.run();
    assert(result.has_value());
    assert(result->status ==
           dnf::AccountAuthenticationStatus::ServiceBusy);
    assert(service.LimiterStats().concurrentPasswordJobs == 0);
}

void TestCharacterListReturnsServiceBusy()
{
    boost::asio::io_context ioContext;
    SaturatedDatabaseExecutor saturated;
    EmptyAccountRepository accounts;
    EmptyAccountPlayerRepository accountPlayers;
    EmptyPlayerRepository players;
    dnf::CharacterListService service(
        ioContext,
        saturated.executor,
        accounts,
        accountPlayers,
        players);
    std::optional<dnf::CharacterListResult> result;

    service.LoadCharacters(
        1,
        [&result](dnf::CharacterListResult value)
        {
            result = std::move(value);
        });

    ioContext.run();
    assert(result.has_value());
    assert(result->status == dnf::CharacterListStatus::ServiceBusy);
    assert(result->characters.empty());
}

void TestCharacterSelectionReturnsServiceBusy()
{
    boost::asio::io_context ioContext;
    SaturatedDatabaseExecutor saturated;
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAuthTicketStore ticketStore(database);
    EmptyAccountPlayerRepository accountPlayers;
    dnf::AuthTicketIssuer ticketIssuer(accountPlayers, ticketStore);
    dnf::CharacterSelectionService service(
        ioContext,
        saturated.executor,
        ticketIssuer);
    std::optional<dnf::CharacterSelectionResult> result;

    service.SelectCharacter(
        1,
        1,
        [&result](dnf::CharacterSelectionResult value)
        {
            result = std::move(value);
        });

    ioContext.run();
    assert(result.has_value());
    assert(result->status ==
           dnf::CharacterSelectionStatus::ServiceBusy);
    assert(!result->authTicket.has_value());
}

void TestPlayerLoginReturnsServiceBusy()
{
    boost::asio::io_context ioContext;
    SaturatedDatabaseExecutor saturated;
    EmptyTicketVerifier tickets;
    EmptyPlayerRepository players;
    dnf::PlayerLoginService service(
        ioContext,
        saturated.executor,
        tickets,
        players);
    std::optional<dnf::PlayerLoginResult> result;

    service.Login(
        "valid-shaped-ticket",
        [&result](dnf::PlayerLoginResult value)
        {
            result = std::move(value);
        });

    ioContext.run();
    assert(result.has_value());
    assert(result->status == dnf::PlayerLoginStatus::ServiceBusy);
    assert(!result->authContext.has_value());
    assert(!result->profile.has_value());
}
} // namespace

int main()
{
    TestAuthenticationReturnsServiceBusy();
    TestCharacterListReturnsServiceBusy();
    TestCharacterSelectionReturnsServiceBusy();
    TestPlayerLoginReturnsServiceBusy();

    std::cout << "All async service backpressure tests passed.\n";
    return 0;
}
