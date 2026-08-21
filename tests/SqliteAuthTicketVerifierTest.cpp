#include "SqliteAuthTicketStore.h"
#include "SqliteAuthTicketVerifier.h"
#include "SqliteDatabase.h"
#include "SqlitePlayerRepository.h"

#include <chrono>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

namespace
{
std::int64_t CurrentUnixTime()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

struct TestContext
{
    TestContext()
        : database(":memory:"),
          playerRepository(database),
          ticketStore(database),
          ticketVerifier(ticketStore)
    {
        playerId = playerRepository.CreatePlayer("Player_1")->playerId;
    }

    dnf::SqliteDatabase database;
    dnf::SqlitePlayerRepository playerRepository;
    dnf::SqliteAuthTicketStore ticketStore;
    dnf::SqliteAuthTicketVerifier ticketVerifier;
    dnf::PlayerId playerId = 0;
};

void TestValidTicketCanOnlyBeUsedOnce()
{
    TestContext context;
    assert(context.ticketStore.IssueTicket(
        "one-time-ticket",
        {10, context.playerId},
        CurrentUnixTime() + 60));

    const std::optional<dnf::AuthContext> verified =
        context.ticketVerifier.Verify("one-time-ticket");
    assert(verified.has_value());
    assert(verified->accountId == 10);
    assert(verified->playerId == context.playerId);
    assert(!context.ticketVerifier.Verify("one-time-ticket").has_value());
}

void TestExpiredTicketIsRejectedAndRemoved()
{
    TestContext context;
    assert(context.ticketStore.IssueTicket(
        "expired-ticket",
        {10, context.playerId},
        CurrentUnixTime() - 1));

    assert(!context.ticketVerifier.Verify("expired-ticket").has_value());
    assert(!context.ticketStore.ConsumeTicket(
        "expired-ticket",
        0).has_value());
}

void TestInvalidOrDuplicateTicketIsRejected()
{
    TestContext context;
    const std::int64_t expiresAt = CurrentUnixTime() + 60;

    assert(!context.ticketStore.IssueTicket(
        "",
        {10, context.playerId},
        expiresAt));
    assert(!context.ticketStore.IssueTicket(
        "invalid-context",
        {},
        expiresAt));
    assert(!context.ticketStore.IssueTicket(
        "missing-player",
        {10, 9999},
        expiresAt));

    assert(context.ticketStore.IssueTicket(
        "duplicate-ticket",
        {10, context.playerId},
        expiresAt));
    assert(!context.ticketStore.IssueTicket(
        "duplicate-ticket",
        {10, context.playerId},
        expiresAt));
}
} // namespace

int main()
{
    TestValidTicketCanOnlyBeUsedOnce();
    TestExpiredTicketIsRejectedAndRemoved();
    TestInvalidOrDuplicateTicketIsRejected();

    std::cout << "All SQLite auth ticket verifier tests passed.\n";
    return 0;
}
