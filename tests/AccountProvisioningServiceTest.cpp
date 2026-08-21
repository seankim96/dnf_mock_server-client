#include "AccountProvisioningService.h"
#include "SqliteAccountRepository.h"
#include "SqliteDatabase.h"

#include <cassert>
#include <iostream>
#include <optional>
#include <string>

namespace
{
class TestPasswordHasher final : public dnf::PasswordHasher
{
public:
    std::optional<std::string> Hash(
        const std::string& password) override
    {
        if (failHashing)
        {
            return std::nullopt;
        }

        return "test-hash:" + password;
    }

    bool Verify(
        const std::string& password,
        const std::string& encodedPasswordHash) override
    {
        return encodedPasswordHash == "test-hash:" + password;
    }

    bool failHashing = false;
};

void TestCreatesHashedAccount()
{
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountRepository repository(database);
    TestPasswordHasher passwordHasher;
    dnf::AccountProvisioningService service(
        repository,
        passwordHasher);

    const dnf::AccountProvisioningResult result =
        service.CreateAccount("account_1", "secret-password");

    assert(result.status ==
           dnf::AccountProvisioningStatus::Success);
    assert(result.accountId != 0);

    const std::optional<dnf::Account> stored =
        repository.FindAccount(result.accountId);
    assert(stored.has_value());
    assert(stored->encodedPasswordHash ==
           "test-hash:secret-password");
    assert(stored->encodedPasswordHash != "secret-password");
}

void TestRejectsInvalidAndDuplicateAccounts()
{
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountRepository repository(database);
    TestPasswordHasher passwordHasher;
    dnf::AccountProvisioningService service(
        repository,
        passwordHasher);

    assert(service.CreateAccount("bad id", "password").status ==
           dnf::AccountProvisioningStatus::InvalidLoginId);
    assert(service.CreateAccount("account_1", "").status ==
           dnf::AccountProvisioningStatus::InvalidPassword);
    assert(service.CreateAccount("account_1", "password").status ==
           dnf::AccountProvisioningStatus::Success);
    assert(service.CreateAccount("ACCOUNT_1", "password").status ==
           dnf::AccountProvisioningStatus::LoginIdAlreadyExists);
}

void TestReportsHashFailure()
{
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountRepository repository(database);
    TestPasswordHasher passwordHasher;
    passwordHasher.failHashing = true;
    dnf::AccountProvisioningService service(
        repository,
        passwordHasher);

    assert(service.CreateAccount("account_1", "password").status ==
           dnf::AccountProvisioningStatus::PasswordHashFailed);
    assert(!repository.FindAccountByLoginId("account_1").has_value());
}
} // namespace

int main()
{
    TestCreatesHashedAccount();
    TestRejectsInvalidAndDuplicateAccounts();
    TestReportsHashFailure();

    std::cout << "All account provisioning service tests passed.\n";
    return 0;
}
