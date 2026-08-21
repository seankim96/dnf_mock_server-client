#include "AuthTicketIssuer.h"
#include "SqliteAccountPlayerRepository.h"
#include "SqliteAccountRepository.h"
#include "SqliteAuthTicketStore.h"
#include "SqliteAuthTicketVerifier.h"
#include "SqliteDatabase.h"
#include "SqlitePlayerRepository.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cctype>
#include <iostream>
#include <stdexcept>

namespace
{
struct TestContext
{
    TestContext()
        : database(":memory:"),
          accountRepository(database),
          playerRepository(database),
          accountPlayerRepository(database),
          ticketStore(database),
          ticketVerifier(ticketStore),
          ticketIssuer(accountPlayerRepository, ticketStore)
    {
        accountId = accountRepository.CreateAccount(
            "account_1",
            "$argon2id$test-only-encoded-hash")->accountId;
        playerId = playerRepository.CreatePlayer("Player_1")->playerId;
        assert(accountPlayerRepository.LinkPlayer(accountId, playerId));
    }

    dnf::SqliteDatabase database;
    dnf::SqliteAccountRepository accountRepository;
    dnf::SqlitePlayerRepository playerRepository;
    dnf::SqliteAccountPlayerRepository accountPlayerRepository;
    dnf::SqliteAuthTicketStore ticketStore;
    dnf::SqliteAuthTicketVerifier ticketVerifier;
    dnf::AuthTicketIssuer ticketIssuer;
    dnf::AccountId accountId = 0;
    dnf::PlayerId playerId = 0;
};

bool IsLowercaseHex(const std::string& value)
{
    return std::all_of(
        value.begin(),
        value.end(),
        [](unsigned char character)
        {
            return std::isdigit(character) != 0 ||
                   (character >= 'a' && character <= 'f');
        });
}

void TestIssuedTicketCanBeVerifiedOnce()
{
    TestContext context;
    const auto beforeIssue = std::chrono::system_clock::now();

    const std::optional<dnf::IssuedAuthTicket> issued =
        context.ticketIssuer.Issue({context.accountId, context.playerId});
    assert(issued.has_value());
    assert(issued->ticket.size() == 64);
    assert(IsLowercaseHex(issued->ticket));

    const auto beforeUnix =
        std::chrono::duration_cast<std::chrono::seconds>(
            beforeIssue.time_since_epoch()).count();
    assert(issued->expiresAtUnix >=
           beforeUnix + dnf::DEFAULT_AUTH_TICKET_LIFETIME.count());

    const std::optional<dnf::AuthContext> verified =
        context.ticketVerifier.Verify(issued->ticket);
    assert(verified.has_value());
    assert(verified->accountId == context.accountId);
    assert(verified->playerId == context.playerId);
    assert(!context.ticketVerifier.Verify(issued->ticket).has_value());
}

void TestEachIssueCreatesADifferentTicket()
{
    TestContext context;
    const auto first = context.ticketIssuer.Issue(
        {context.accountId, context.playerId});
    const auto second = context.ticketIssuer.Issue(
        {context.accountId, context.playerId});

    assert(first.has_value());
    assert(second.has_value());
    assert(first->ticket != second->ticket);
}

void TestInvalidContextOrLifetimeIsRejected()
{
    TestContext context;
    assert(!context.ticketIssuer.Issue({}).has_value());
    assert(!context.ticketIssuer.Issue(
        {context.accountId, 9999}).has_value());
    assert(!context.ticketIssuer.Issue(
        {context.accountId + 1, context.playerId}).has_value());

    bool rejectedLifetime = false;
    try
    {
        dnf::AuthTicketIssuer invalidIssuer(
            context.accountPlayerRepository,
            context.ticketStore,
            std::chrono::seconds::zero());
    }
    catch (const std::invalid_argument&)
    {
        rejectedLifetime = true;
    }
    assert(rejectedLifetime);
}
} // namespace

int main()
{
    TestIssuedTicketCanBeVerifiedOnce();
    TestEachIssueCreatesADifferentTicket();
    TestInvalidContextOrLifetimeIsRejected();

    std::cout << "All auth ticket issuer tests passed.\n";
    return 0;
}
