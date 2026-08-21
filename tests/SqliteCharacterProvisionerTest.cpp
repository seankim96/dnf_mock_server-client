#include "SqliteAccountPlayerRepository.h"
#include "SqliteAccountRepository.h"
#include "SqliteCharacterProvisioner.h"
#include "SqlitePlayerRepository.h"

#include <sqlite3.h>

#include <cassert>
#include <iostream>

namespace
{
constexpr const char* TEST_ENCODED_PASSWORD_HASH =
    "$argon2id$test-only-encoded-hash";

dnf::Account CreateAccount(
    dnf::SqliteAccountRepository& repository,
    const std::string& loginId)
{
    return repository.CreateAccount(
        loginId,
        TEST_ENCODED_PASSWORD_HASH).value();
}

void TestCreatesPlayerAndOwnershipTogether()
{
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountRepository accountRepository(database);
    dnf::SqlitePlayerRepository playerRepository(database);
    dnf::SqliteAccountPlayerRepository ownershipRepository(database);
    dnf::SqliteCharacterProvisioner provisioner(database);

    const dnf::Account account =
        CreateAccount(accountRepository, "account_1");
    const dnf::CharacterProvisioningResult result =
        provisioner.CreateOwnedPlayer(account.accountId, "Player_1");

    assert(result.status ==
           dnf::CharacterProvisioningStatus::Success);
    assert(result.playerId != 0);
    assert(playerRepository.FindPlayer(result.playerId).has_value());
    assert(ownershipRepository.OwnsPlayer(
        account.accountId,
        result.playerId));
}

void TestRejectsMissingAccountAndDuplicateName()
{
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountRepository accountRepository(database);
    dnf::SqlitePlayerRepository playerRepository(database);
    dnf::SqliteCharacterProvisioner provisioner(database);

    assert(provisioner.CreateOwnedPlayer(9999, "Player_1").status ==
           dnf::CharacterProvisioningStatus::AccountNotFound);
    assert(!playerRepository.FindPlayerByName("Player_1").has_value());

    const dnf::Account firstAccount =
        CreateAccount(accountRepository, "account_1");
    const dnf::Account secondAccount =
        CreateAccount(accountRepository, "account_2");
    assert(provisioner.CreateOwnedPlayer(
        firstAccount.accountId,
        "Player_1").status ==
           dnf::CharacterProvisioningStatus::Success);
    assert(provisioner.CreateOwnedPlayer(
        secondAccount.accountId,
        "Player_1").status ==
           dnf::CharacterProvisioningStatus::PlayerNameAlreadyExists);
}

void TestOwnershipFailureRollsBackPlayer()
{
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountRepository accountRepository(database);
    dnf::SqlitePlayerRepository playerRepository(database);
    dnf::SqliteCharacterProvisioner provisioner(database);

    const dnf::Account account =
        CreateAccount(accountRepository, "account_1");

    const int triggerResult = sqlite3_exec(
        database.Handle(),
        "CREATE TRIGGER reject_test_ownership "
        "BEFORE INSERT ON account_players "
        "BEGIN SELECT RAISE(ABORT, 'test failure'); END;",
        nullptr,
        nullptr,
        nullptr);
    assert(triggerResult == SQLITE_OK);

    bool failed = false;
    try
    {
        provisioner.CreateOwnedPlayer(
            account.accountId,
            "RollbackPlayer");
    }
    catch (const dnf::DatabaseError&)
    {
        failed = true;
    }

    assert(failed);
    assert(!playerRepository.FindPlayerByName(
        "RollbackPlayer").has_value());
}

void TestRejectsInvalidInput()
{
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteCharacterProvisioner provisioner(database);

    assert(provisioner.CreateOwnedPlayer(0, "Player_1").status ==
           dnf::CharacterProvisioningStatus::InvalidInput);
    assert(provisioner.CreateOwnedPlayer(1, "Invalid Name").status ==
           dnf::CharacterProvisioningStatus::InvalidInput);
}
} // namespace

int main()
{
    TestCreatesPlayerAndOwnershipTogether();
    TestRejectsMissingAccountAndDuplicateName();
    TestOwnershipFailureRollsBackPlayer();
    TestRejectsInvalidInput();

    std::cout << "All SQLite character provisioner tests passed.\n";
    return 0;
}
