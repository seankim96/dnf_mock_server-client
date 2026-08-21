#include "SqliteAccountPlayerRepository.h"
#include "SqliteAccountRepository.h"
#include "SqlitePlayerRepository.h"

#include <cassert>
#include <iostream>

namespace
{
constexpr const char* TEST_ENCODED_PASSWORD_HASH =
    "$argon2id$test-only-encoded-hash";

void TestOneAccountCanOwnMultiplePlayers()
{
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountRepository accountRepository(database);
    dnf::SqlitePlayerRepository playerRepository(database);
    dnf::SqliteAccountPlayerRepository accountPlayers(database);

    const dnf::Account account = accountRepository.CreateAccount(
        "account_1",
        TEST_ENCODED_PASSWORD_HASH).value();
    const dnf::PlayerProfile firstPlayer =
        playerRepository.CreatePlayer("Player_1").value();
    const dnf::PlayerProfile secondPlayer =
        playerRepository.CreatePlayer("Player_2").value();

    assert(accountPlayers.LinkPlayer(
        account.accountId,
        firstPlayer.playerId));
    assert(accountPlayers.LinkPlayer(
        account.accountId,
        secondPlayer.playerId));
    assert(accountPlayers.OwnsPlayer(
        account.accountId,
        firstPlayer.playerId));
    assert(accountPlayers.OwnsPlayer(
        account.accountId,
        secondPlayer.playerId));

    const std::vector<dnf::PlayerId> playerIds =
        accountPlayers.FindPlayerIds(account.accountId);
    assert(playerIds.size() == 2);
    assert(playerIds[0] == firstPlayer.playerId);
    assert(playerIds[1] == secondPlayer.playerId);
}

void TestPlayerCanOnlyHaveOneOwner()
{
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountRepository accountRepository(database);
    dnf::SqlitePlayerRepository playerRepository(database);
    dnf::SqliteAccountPlayerRepository accountPlayers(database);

    const dnf::Account firstAccount = accountRepository.CreateAccount(
        "account_1",
        TEST_ENCODED_PASSWORD_HASH).value();
    const dnf::Account secondAccount = accountRepository.CreateAccount(
        "account_2",
        TEST_ENCODED_PASSWORD_HASH).value();
    const dnf::PlayerProfile player =
        playerRepository.CreatePlayer("Player_1").value();

    assert(accountPlayers.LinkPlayer(
        firstAccount.accountId,
        player.playerId));
    assert(!accountPlayers.LinkPlayer(
        secondAccount.accountId,
        player.playerId));
    assert(!accountPlayers.OwnsPlayer(
        secondAccount.accountId,
        player.playerId));
}

void TestInvalidOrMissingIdsAreRejected()
{
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountPlayerRepository accountPlayers(database);

    assert(!accountPlayers.LinkPlayer(0, 1));
    assert(!accountPlayers.LinkPlayer(1, 0));
    assert(!accountPlayers.LinkPlayer(1, 1));
    assert(!accountPlayers.OwnsPlayer(1, 1));
    assert(accountPlayers.FindPlayerIds(0).empty());
    assert(accountPlayers.FindPlayerIds(1).empty());
}
} // namespace

int main()
{
    TestOneAccountCanOwnMultiplePlayers();
    TestPlayerCanOnlyHaveOneOwner();
    TestInvalidOrMissingIdsAreRejected();

    std::cout << "All SQLite account-player repository tests passed.\n";
    return 0;
}
