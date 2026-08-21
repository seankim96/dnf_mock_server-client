#include "AccountAuthenticationService.h"
#include "SqliteAccountRepository.h"
#include "SqliteDatabase.h"

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
class TestPasswordHasher final : public dnf::PasswordHasher
{
public:
    std::optional<std::string> Hash(
        const std::string& password) override
    {
        if (password.empty())
        {
            return std::nullopt;
        }

        return "test-hash:" + password;
    }

    bool Verify(
        const std::string& password,
        const std::string& encodedPasswordHash) override
    {
        ++verifyCount;
        return encodedPasswordHash == "test-hash:" + password;
    }

    int verifyCount = 0;
};

dnf::AccountAuthenticationResult WaitForAuthentication(
    dnf::AccountAuthenticationService& authenticationService,
    boost::asio::io_context& ioContext,
    const std::string& loginId,
    const std::string& password,
    std::thread::id* callbackThreadId = nullptr)
{
    auto workGuard = boost::asio::make_work_guard(ioContext);
    std::optional<dnf::AccountAuthenticationResult> output;

    authenticationService.Authenticate(
        loginId,
        password,
        [&](dnf::AccountAuthenticationResult result)
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

void TestCorrectPasswordAuthenticatesOnIoThread()
{
    boost::asio::io_context ioContext;
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountRepository accountRepository(database);
    TestPasswordHasher passwordHasher;
    const dnf::Account account = accountRepository.CreateAccount(
        "account_1",
        passwordHasher.Hash("correct-password").value()).value();
    dnf::DatabaseExecutor databaseExecutor(1);
    dnf::AccountAuthenticationService authenticationService(
        ioContext,
        databaseExecutor,
        accountRepository,
        passwordHasher);

    const std::thread::id ioThreadId = std::this_thread::get_id();
    std::thread::id callbackThreadId;
    const dnf::AccountAuthenticationResult result =
        WaitForAuthentication(
            authenticationService,
            ioContext,
            "ACCOUNT_1",
            "correct-password",
            &callbackThreadId);

    assert(result.status ==
           dnf::AccountAuthenticationStatus::Success);
    assert(result.accountId == account.accountId);
    assert(callbackThreadId == ioThreadId);
}

void TestWrongPasswordAndMissingAccountHaveSameResult()
{
    boost::asio::io_context ioContext;
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountRepository accountRepository(database);
    TestPasswordHasher passwordHasher;
    assert(accountRepository.CreateAccount(
        "account_1",
        passwordHasher.Hash("correct-password").value()));
    dnf::DatabaseExecutor databaseExecutor(1);
    dnf::AccountAuthenticationService authenticationService(
        ioContext,
        databaseExecutor,
        accountRepository,
        passwordHasher);

    const int verifyCountBefore = passwordHasher.verifyCount;
    const dnf::AccountAuthenticationResult wrongPassword =
        WaitForAuthentication(
            authenticationService,
            ioContext,
            "account_1",
            "wrong-password");
    const dnf::AccountAuthenticationResult missingAccount =
        WaitForAuthentication(
            authenticationService,
            ioContext,
            "missing_account",
            "wrong-password");

    assert(wrongPassword.status ==
           dnf::AccountAuthenticationStatus::InvalidCredentials);
    assert(missingAccount.status ==
           dnf::AccountAuthenticationStatus::InvalidCredentials);
    assert(!wrongPassword.accountId.has_value());
    assert(!missingAccount.accountId.has_value());
    assert(passwordHasher.verifyCount == verifyCountBefore + 2);
}

class FailingAccountRepository final : public dnf::AccountRepository
{
public:
    std::optional<dnf::Account> FindAccount(dnf::AccountId) override
    {
        return std::nullopt;
    }

    std::optional<dnf::Account> FindAccountByLoginId(
        const std::string&) override
    {
        throw std::runtime_error("storage unavailable");
    }

    std::optional<dnf::Account> CreateAccount(
        const std::string&,
        const std::string&) override
    {
        return std::nullopt;
    }
};

void TestServiceFailureDoesNotExposeAccountData()
{
    boost::asio::io_context ioContext;
    FailingAccountRepository accountRepository;
    TestPasswordHasher passwordHasher;
    dnf::DatabaseExecutor databaseExecutor(1);
    dnf::AccountAuthenticationService authenticationService(
        ioContext,
        databaseExecutor,
        accountRepository,
        passwordHasher);

    const dnf::AccountAuthenticationResult result =
        WaitForAuthentication(
            authenticationService,
            ioContext,
            "account_1",
            "password");

    assert(result.status ==
           dnf::AccountAuthenticationStatus::ServiceError);
    assert(!result.accountId.has_value());
}

void TestInvalidInputCompletesAsynchronously()
{
    boost::asio::io_context ioContext;
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountRepository accountRepository(database);
    TestPasswordHasher passwordHasher;
    dnf::DatabaseExecutor databaseExecutor(1);
    dnf::AccountAuthenticationService authenticationService(
        ioContext,
        databaseExecutor,
        accountRepository,
        passwordHasher);

    const dnf::AccountAuthenticationResult result =
        WaitForAuthentication(
            authenticationService,
            ioContext,
            "bad id",
            "password");
    assert(result.status ==
           dnf::AccountAuthenticationStatus::InvalidCredentials);
    assert(!result.accountId.has_value());
}
} // namespace

int main()
{
    TestCorrectPasswordAuthenticatesOnIoThread();
    TestWrongPasswordAndMissingAccountHaveSameResult();
    TestServiceFailureDoesNotExposeAccountData();
    TestInvalidInputCompletesAsynchronously();

    std::cout << "All account authentication service tests passed.\n";
    return 0;
}
