#include "SqliteAccountRepository.h"

#include <cassert>
#include <iostream>
#include <string>

namespace
{
constexpr const char* TEST_ENCODED_PASSWORD_HASH =
    "$argon2id$test-only-encoded-hash";

void TestCreateAndFindAccount()
{
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountRepository repository(database);

    const std::optional<dnf::Account> created =
        repository.CreateAccount(
            "account_1",
            TEST_ENCODED_PASSWORD_HASH);
    assert(created.has_value());
    assert(created->accountId != 0);
    assert(created->loginId == "account_1");
    assert(created->encodedPasswordHash == TEST_ENCODED_PASSWORD_HASH);

    const std::optional<dnf::Account> byId =
        repository.FindAccount(created->accountId);
    const std::optional<dnf::Account> byLoginId =
        repository.FindAccountByLoginId("ACCOUNT_1");
    assert(byId.has_value());
    assert(byLoginId.has_value());
    assert(byId->accountId == byLoginId->accountId);
    assert(byLoginId->encodedPasswordHash ==
           TEST_ENCODED_PASSWORD_HASH);
}

void TestDuplicateAndInvalidAccountIsRejected()
{
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountRepository repository(database);

    assert(repository.CreateAccount(
        "account_1",
        TEST_ENCODED_PASSWORD_HASH));
    assert(!repository.CreateAccount(
        "ACCOUNT_1",
        TEST_ENCODED_PASSWORD_HASH));
    assert(!repository.CreateAccount(
        "bad id",
        TEST_ENCODED_PASSWORD_HASH));
    assert(!repository.CreateAccount("valid_id", ""));

    const std::string longLoginId(
        dnf::MAX_ACCOUNT_LOGIN_ID_LENGTH + 1,
        'a');
    const std::string longPasswordHash(
        dnf::MAX_ENCODED_PASSWORD_HASH_LENGTH + 1,
        'a');
    assert(!repository.CreateAccount(
        longLoginId,
        TEST_ENCODED_PASSWORD_HASH));
    assert(!repository.CreateAccount("valid_id", longPasswordHash));
}

void TestMissingAccountReturnsEmpty()
{
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountRepository repository(database);

    assert(!repository.FindAccount(0).has_value());
    assert(!repository.FindAccount(9999).has_value());
    assert(!repository.FindAccountByLoginId("missing_account").has_value());
    assert(!repository.FindAccountByLoginId("bad id").has_value());
}
} // namespace

int main()
{
    TestCreateAndFindAccount();
    TestDuplicateAndInvalidAccountIsRejected();
    TestMissingAccountReturnsEmpty();

    std::cout << "All SQLite account repository tests passed.\n";
    return 0;
}
