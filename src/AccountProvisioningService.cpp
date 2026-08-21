#include "AccountProvisioningService.h"

#include "Account.h"

#include <openssl/crypto.h>

#include <optional>

namespace dnf
{
namespace
{
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
} // namespace

AccountProvisioningService::AccountProvisioningService(
    AccountRepository& accountRepository,
    PasswordHasher& passwordHasher)
    : accountRepository_(accountRepository),
      passwordHasher_(passwordHasher)
{
}

AccountProvisioningResult AccountProvisioningService::CreateAccount(
    std::string loginId,
    std::string password)
{
    PasswordCleanup passwordCleanup(password);

    if (!IsValidAccountLoginId(loginId))
    {
        return {AccountProvisioningStatus::InvalidLoginId, 0};
    }

    if (password.empty() || password.size() > MAX_PASSWORD_LENGTH)
    {
        return {AccountProvisioningStatus::InvalidPassword, 0};
    }

    const std::optional<std::string> encodedPasswordHash =
        passwordHasher_.Hash(password);
    if (!encodedPasswordHash.has_value() ||
        !IsValidEncodedPasswordHash(encodedPasswordHash.value()))
    {
        return {AccountProvisioningStatus::PasswordHashFailed, 0};
    }

    const std::optional<Account> account =
        accountRepository_.CreateAccount(
            loginId,
            encodedPasswordHash.value());
    if (!account.has_value())
    {
        return {
            AccountProvisioningStatus::LoginIdAlreadyExists,
            0};
    }

    return {
        AccountProvisioningStatus::Success,
        account->accountId};
}
} // namespace dnf
