#include "CharacterSelectionService.h"
#include "SqliteAccountPlayerRepository.h"
#include "SqliteAccountRepository.h"
#include "SqliteAuthTicketStore.h"
#include "SqliteAuthTicketVerifier.h"
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
dnf::CharacterSelectionResult WaitForSelection(
    dnf::CharacterSelectionService& selectionService,
    boost::asio::io_context& ioContext,
    dnf::AccountId accountId,
    dnf::PlayerId playerId,
    std::thread::id* callbackThreadId = nullptr)
{
    auto workGuard = boost::asio::make_work_guard(ioContext);
    std::optional<dnf::CharacterSelectionResult> output;

    selectionService.SelectCharacter(
        accountId,
        playerId,
        [&](dnf::CharacterSelectionResult result)
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
          ticketStore(database),
          ticketVerifier(ticketStore),
          ticketIssuer(accountPlayerRepository, ticketStore),
          databaseExecutor(1),
          selectionService(ioContext, databaseExecutor, ticketIssuer)
    {
        accountId = accountRepository.CreateAccount(
            "account_1",
            "$argon2id$test-only-encoded-hash")->accountId;
        playerId = playerRepository.CreatePlayer("Player_1")->playerId;
        assert(accountPlayerRepository.LinkPlayer(accountId, playerId));
    }

    boost::asio::io_context ioContext;
    dnf::SqliteDatabase database;
    dnf::SqliteAccountRepository accountRepository;
    dnf::SqlitePlayerRepository playerRepository;
    dnf::SqliteAccountPlayerRepository accountPlayerRepository;
    dnf::SqliteAuthTicketStore ticketStore;
    dnf::SqliteAuthTicketVerifier ticketVerifier;
    dnf::AuthTicketIssuer ticketIssuer;
    dnf::DatabaseExecutor databaseExecutor;
    dnf::CharacterSelectionService selectionService;
    dnf::AccountId accountId = 0;
    dnf::PlayerId playerId = 0;
};

void TestOwnedCharacterProducesGameTicketOnIoThread()
{
    TestContext context;
    const std::thread::id ioThreadId = std::this_thread::get_id();
    std::thread::id callbackThreadId;

    const dnf::CharacterSelectionResult result = WaitForSelection(
        context.selectionService,
        context.ioContext,
        context.accountId,
        context.playerId,
        &callbackThreadId);

    assert(result.status == dnf::CharacterSelectionStatus::Success);
    assert(result.authTicket.has_value());
    assert(callbackThreadId == ioThreadId);

    const std::optional<dnf::AuthContext> verified =
        context.ticketVerifier.Verify(result.authTicket->ticket);
    assert(verified.has_value());
    assert(verified->accountId == context.accountId);
    assert(verified->playerId == context.playerId);
}

void TestCharacterOwnedByAnotherAccountIsRejected()
{
    TestContext context;
    const dnf::AccountId otherAccountId =
        context.accountRepository.CreateAccount(
            "account_2",
            "$argon2id$test-only-encoded-hash")->accountId;

    const dnf::CharacterSelectionResult result = WaitForSelection(
        context.selectionService,
        context.ioContext,
        otherAccountId,
        context.playerId);

    assert(result.status ==
           dnf::CharacterSelectionStatus::InvalidSelection);
    assert(!result.authTicket.has_value());
}

void TestInvalidSelectionCompletesAsynchronously()
{
    TestContext context;
    const dnf::CharacterSelectionResult result = WaitForSelection(
        context.selectionService,
        context.ioContext,
        0,
        context.playerId);

    assert(result.status ==
           dnf::CharacterSelectionStatus::InvalidSelection);
    assert(!result.authTicket.has_value());
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
        throw std::runtime_error("storage unavailable");
    }

    std::vector<dnf::PlayerId> FindPlayerIds(
        dnf::AccountId) override
    {
        return {};
    }
};

void TestServiceFailureDoesNotReturnTicket()
{
    boost::asio::io_context ioContext;
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAuthTicketStore ticketStore(database);
    FailingAccountPlayerRepository accountPlayers;
    dnf::AuthTicketIssuer ticketIssuer(accountPlayers, ticketStore);
    dnf::DatabaseExecutor databaseExecutor(1);
    dnf::CharacterSelectionService selectionService(
        ioContext,
        databaseExecutor,
        ticketIssuer);

    const dnf::CharacterSelectionResult result = WaitForSelection(
        selectionService,
        ioContext,
        1,
        1);

    assert(result.status == dnf::CharacterSelectionStatus::ServiceError);
    assert(!result.authTicket.has_value());
}
} // namespace

int main()
{
    TestOwnedCharacterProducesGameTicketOnIoThread();
    TestCharacterOwnedByAnotherAccountIsRejected();
    TestInvalidSelectionCompletesAsynchronously();
    TestServiceFailureDoesNotReturnTicket();

    std::cout << "All character selection service tests passed.\n";
    return 0;
}
