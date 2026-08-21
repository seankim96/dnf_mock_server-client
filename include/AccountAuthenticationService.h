#pragma once

#include "AccountId.h"
#include "AccountRepository.h"
#include "DatabaseExecutor.h"
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
        PasswordHasher& passwordHasher);

    void Authenticate(
        std::string loginId,
        std::string password,
        CompletionHandler completionHandler);

private:
    boost::asio::io_context& ioContext_;
    DatabaseExecutor& databaseExecutor_;
    AccountRepository& accountRepository_;
    PasswordHasher& passwordHasher_;
    std::string dummyEncodedPasswordHash_;
};
} // namespace dnf
