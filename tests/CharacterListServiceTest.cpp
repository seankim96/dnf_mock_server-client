#include "CharacterListService.h"
#include "SqliteAccountPlayerRepository.h"
#include "SqliteAccountRepository.h"
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
#include <vector>

namespace
{
dnf::CharacterListResult WaitForCharacterList(
    dnf::CharacterListService& characterListService,
    boost::asio::io_context& ioContext,
    dnf::AccountId accountId,
    std::thread::id* callbackThreadId = nullptr)
{
    auto workGuard = boost::asio::make_work_guard(ioContext);
    std::optional<dnf::CharacterListResult> output;

    characterListService.LoadCharacters(
        accountId,
        [&](dnf::CharacterListResult result)
        {
            if (callbackThreadId != nullptr)
            {
                *callbackThreadId = std::this_thread::get_id();
            }

            output = std::move(result);
            workGuard.reset();
        });

    assert(!output.has_value());
    ioContext.run();
    ioContext.restart();
    assert(output.has_value());
    return output.value();
}

struct TestContext
{
    TestContext()
        : database(":memory:"),
          accountRepository(database),
          playerRepository(database),
          accountPlayerRepository(database),
          databaseExecutor(1),
          characterListService(
              ioContext,
              databaseExecutor,
              accountRepository,
              accountPlayerRepository,
              playerRepository)
    {
    }

    boost::asio::io_context ioContext;
    dnf::SqliteDatabase database;
    dnf::SqliteAccountRepository accountRepository;
    dnf::SqlitePlayerRepository playerRepository;
    dnf::SqliteAccountPlayerRepository accountPlayerRepository;
    dnf::DatabaseExecutor databaseExecutor;
    dnf::CharacterListService characterListService;
};

dnf::Account CreateAccount(TestContext& context, const std::string& loginId)
{
    return context.accountRepository.CreateAccount(
        loginId,
        "$argon2id$test-only-encoded-hash").value();
}

void TestOwnedCharacterSummariesAreLoadedOnIoThread()
{
    TestContext context;
    const dnf::Account account = CreateAccount(context, "account_1");
    dnf::PlayerProfile first =
        context.playerRepository.CreatePlayer("Player_1").value();
    dnf::PlayerProfile second =
        context.playerRepository.CreatePlayer("Player_2").value();
    first.level = 10;
    second.level = 20;
    assert(context.playerRepository.SavePlayer(first));
    assert(context.playerRepository.SavePlayer(second));
    assert(context.accountPlayerRepository.LinkPlayer(
        account.accountId,
        second.playerId));
    assert(context.accountPlayerRepository.LinkPlayer(
        account.accountId,
        first.playerId));

    const std::thread::id ioThreadId = std::this_thread::get_id();
    std::thread::id callbackThreadId;
    const dnf::CharacterListResult result = WaitForCharacterList(
        context.characterListService,
        context.ioContext,
        account.accountId,
        &callbackThreadId);

    assert(result.status == dnf::CharacterListStatus::Success);
    assert(result.characters.size() == 2);
    assert(result.characters[0].playerId == first.playerId);
    assert(result.characters[0].name == "Player_1");
    assert(result.characters[0].level == 10);
    assert(result.characters[1].playerId == second.playerId);
    assert(result.characters[1].name == "Player_2");
    assert(result.characters[1].level == 20);
    assert(callbackThreadId == ioThreadId);
}

void TestAccountWithoutCharactersReturnsEmptyList()
{
    TestContext context;
    const dnf::Account account = CreateAccount(context, "account_1");

    const dnf::CharacterListResult result = WaitForCharacterList(
        context.characterListService,
        context.ioContext,
        account.accountId);

    assert(result.status == dnf::CharacterListStatus::Success);
    assert(result.characters.empty());
}

void TestMissingAndInvalidAccountAreRejectedAsynchronously()
{
    TestContext context;

    const dnf::CharacterListResult missing = WaitForCharacterList(
        context.characterListService,
        context.ioContext,
        9999);
    const dnf::CharacterListResult invalid = WaitForCharacterList(
        context.characterListService,
        context.ioContext,
        0);

    assert(missing.status == dnf::CharacterListStatus::AccountNotFound);
    assert(invalid.status == dnf::CharacterListStatus::AccountNotFound);
    assert(missing.characters.empty());
    assert(invalid.characters.empty());
}

class FailingAccountPlayerRepository final
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
        throw std::runtime_error("storage unavailable");
    }
};

void TestServiceFailureReturnsNoPartialList()
{
    boost::asio::io_context ioContext;
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountRepository accountRepository(database);
    dnf::SqlitePlayerRepository playerRepository(database);
    FailingAccountPlayerRepository accountPlayers;
    const dnf::Account account = accountRepository.CreateAccount(
        "account_1",
        "$argon2id$test-only-encoded-hash").value();
    dnf::DatabaseExecutor databaseExecutor(1);
    dnf::CharacterListService characterListService(
        ioContext,
        databaseExecutor,
        accountRepository,
        accountPlayers,
        playerRepository);

    const dnf::CharacterListResult result = WaitForCharacterList(
        characterListService,
        ioContext,
        account.accountId);

    assert(result.status == dnf::CharacterListStatus::ServiceError);
    assert(result.characters.empty());
}
} // namespace

int main()
{
    TestOwnedCharacterSummariesAreLoadedOnIoThread();
    TestAccountWithoutCharactersReturnsEmptyList();
    TestMissingAndInvalidAccountAreRejectedAsynchronously();
    TestServiceFailureReturnsNoPartialList();

    std::cout << "All character list service tests passed.\n";
    return 0;
}
