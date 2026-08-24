#include "AccountAuthenticationService.h"

#include <boost/asio/post.hpp>

#include <openssl/crypto.h>

#include <stdexcept>
#include <memory>
#include <utility>

namespace dnf
{
namespace
{
constexpr char DUMMY_PASSWORD[] =
    "account-authentication-timing-placeholder";

struct AuthenticationWork
{
    ~AuthenticationWork()
    {
        OPENSSL_cleanse(password.data(), password.size());
    }

    std::string loginId;
    std::string password;
    AccountAuthenticationService::CompletionHandler completionHandler;
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
    PasswordHasher& passwordHasher,
    LoginAttemptLimiterOptions limiterOptions)
    : ioContext_(ioContext),
      databaseExecutor_(databaseExecutor),
      accountRepository_(accountRepository),
      passwordHasher_(passwordHasher),
      loginAttemptLimiter_(std::move(limiterOptions))
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
    Authenticate(
        "unknown",
        std::move(loginId),
        std::move(password),
        std::move(completionHandler));
}

void AccountAuthenticationService::Authenticate(
    std::string clientAddress,
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

    LoginAdmission admission = loginAttemptLimiter_.TryAcquire(
        clientAddress,
        loginId);
    if (admission.status != LoginAdmissionStatus::Accepted)
    {
        OPENSSL_cleanse(password.data(), password.size());
        const AccountAuthenticationStatus status =
            admission.status == LoginAdmissionStatus::RateLimited
            ? AccountAuthenticationStatus::RateLimited
            : AccountAuthenticationStatus::ServiceBusy;
        PostCompletion(
            ioContext_,
            std::move(completionHandler),
            {status, std::nullopt});
        return;
    }

    auto work = std::make_shared<AuthenticationWork>();
    work->loginId = std::move(loginId);
    work->password = std::move(password);
    work->completionHandler = std::move(completionHandler);

    boost::asio::io_context* ioContext = &ioContext_;
    AccountRepository* accountRepository = &accountRepository_;
    PasswordHasher* passwordHasher = &passwordHasher_;
    const std::string* dummyHash = &dummyEncodedPasswordHash_;

    const bool queued = databaseExecutor_.TryPost(
        [ioContext,
         accountRepository,
         passwordHasher,
         dummyHash,
         work,
         permit = std::move(admission.permit)]() mutable
        {
            (void)permit;
            AccountAuthenticationResult result;

            try
            {
                const std::optional<Account> account =
                    accountRepository->FindAccountByLoginId(
                        work->loginId);
                const std::string& encodedHash = account.has_value()
                    ? account->encodedPasswordHash
                    : *dummyHash;
                const bool passwordMatches =
                    passwordHasher->Verify(
                        work->password,
                        encodedHash);

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
                std::move(work->completionHandler),
                std::move(result));
        });

    if (!queued)
    {
        PostCompletion(
            ioContext_,
            std::move(work->completionHandler),
            {AccountAuthenticationStatus::ServiceBusy,
             std::nullopt});
    }
}

LoginAttemptLimiterStats
AccountAuthenticationService::LimiterStats() const
{
    return loginAttemptLimiter_.Stats();
}
} // namespace dnf
