#pragma once

#include "AccountId.h"
#include "AccountRepository.h"
#include "DatabaseExecutor.h"
#include "LoginAttemptLimiter.h"
#include "PasswordHasher.h"

#include <boost/asio/io_context.hpp>

#include <functional>
#include <optional>
#include <string>

namespace dnf
{
enum class AccountAuthenticationStatus
{
    Success,
    InvalidCredentials,
    RateLimited,
    ServiceBusy,
    ServiceError
};

struct AccountAuthenticationResult
{
    AccountAuthenticationStatus status =
        AccountAuthenticationStatus::InvalidCredentials;
    std::optional<AccountId> accountId;
};

class AccountAuthenticationService
{
public:
    using CompletionHandler =
        std::function<void(AccountAuthenticationResult)>;

    AccountAuthenticationService(
        boost::asio::io_context& ioContext,
        DatabaseExecutor& databaseExecutor,
        AccountRepository& accountRepository,
        PasswordHasher& passwordHasher,
        LoginAttemptLimiterOptions limiterOptions = {});

    void Authenticate(
        std::string loginId,
        std::string password,
        CompletionHandler completionHandler);

    void Authenticate(
        std::string clientAddress,
        std::string loginId,
        std::string password,
        CompletionHandler completionHandler);

    LoginAttemptLimiterStats LimiterStats() const;

private:
    boost::asio::io_context& ioContext_;
    DatabaseExecutor& databaseExecutor_;
    AccountRepository& accountRepository_;
    PasswordHasher& passwordHasher_;
    LoginAttemptLimiter loginAttemptLimiter_;
    std::string dummyEncodedPasswordHash_;
};
} // namespace dnf
