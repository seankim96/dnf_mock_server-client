#include "AccountAuthenticationService.h"

#include <boost/asio/post.hpp>

#include <openssl/crypto.h>

#include <stdexcept>
#include <utility>

namespace dnf
{
namespace
{
constexpr char DUMMY_PASSWORD[] =
    "account-authentication-timing-placeholder";

class PasswordCleanup
{
public:
    explicit PasswordCleanup(std::string& password)
        : password_(password)
    {
    }

    ~PasswordCleanup()
    {
        OPENSSL_cleanse(password_.data(), password_.size());
    }

private:
    std::string& password_;
};

void PostCompletion(
    boost::asio::io_context& ioContext,
    AccountAuthenticationService::CompletionHandler completionHandler,
    AccountAuthenticationResult result)
{
    boost::asio::post(
        ioContext,
        [completionHandler = std::move(completionHandler),
         result = std::move(result)]() mutable
        {
            completionHandler(std::move(result));
        });
}

bool IsValidPasswordInput(const std::string& password)
{
    return !password.empty() &&
           password.size() <= MAX_PASSWORD_LENGTH;
}
} // namespace

AccountAuthenticationService::AccountAuthenticationService(
    boost::asio::io_context& ioContext,
    DatabaseExecutor& databaseExecutor,
    AccountRepository& accountRepository,
    PasswordHasher& passwordHasher)
    : ioContext_(ioContext),
      databaseExecutor_(databaseExecutor),
      accountRepository_(accountRepository),
      passwordHasher_(passwordHasher)
{
    const std::optional<std::string> dummyHash =
        passwordHasher_.Hash(DUMMY_PASSWORD);
    if (!dummyHash.has_value())
    {
        throw std::runtime_error(
            "Failed to create authentication timing hash");
    }

    dummyEncodedPasswordHash_ = dummyHash.value();
}

void AccountAuthenticationService::Authenticate(
    std::string loginId,
    std::string password,
    CompletionHandler completionHandler)
{
    if (!completionHandler)
    {
        throw std::invalid_argument(
            "Authentication completion handler must not be empty");
    }

    if (!IsValidAccountLoginId(loginId) ||
        !IsValidPasswordInput(password))
    {
        OPENSSL_cleanse(password.data(), password.size());
        PostCompletion(
            ioContext_,
            std::move(completionHandler),
            {AccountAuthenticationStatus::InvalidCredentials,
             std::nullopt});
        return;
    }

    boost::asio::io_context* ioContext = &ioContext_;
    AccountRepository* accountRepository = &accountRepository_;
    PasswordHasher* passwordHasher = &passwordHasher_;
    const std::string dummyHash = dummyEncodedPasswordHash_;

    databaseExecutor_.Post(
        [ioContext,
         accountRepository,
         passwordHasher,
         dummyHash,
         loginId = std::move(loginId),
         password = std::move(password),
         completionHandler = std::move(completionHandler)]() mutable
        {
            PasswordCleanup passwordCleanup(password);
            AccountAuthenticationResult result;

            try
            {
                const std::optional<Account> account =
                    accountRepository->FindAccountByLoginId(loginId);
                const std::string& encodedHash = account.has_value()
                    ? account->encodedPasswordHash
                    : dummyHash;
                const bool passwordMatches =
                    passwordHasher->Verify(password, encodedHash);

                if (account.has_value() && passwordMatches)
                {
                    result.status =
                        AccountAuthenticationStatus::Success;
                    result.accountId = account->accountId;
                }
                else
                {
                    result.status =
                        AccountAuthenticationStatus::InvalidCredentials;
                }
            }
            catch (...)
            {
                result.status =
                    AccountAuthenticationStatus::ServiceError;
                result.accountId = std::nullopt;
            }

            PostCompletion(
                *ioContext,
                std::move(completionHandler),
                std::move(result));
        });
}
} // namespace dnf
