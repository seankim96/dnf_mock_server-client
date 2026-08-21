#pragma once

#include "AccountId.h"
#include "AccountRepository.h"
#include "PasswordHasher.h"

#include <string>

namespace dnf
{
enum class AccountProvisioningStatus
{
    Success,
    InvalidLoginId,
    InvalidPassword,
    PasswordHashFailed,
    LoginIdAlreadyExists
};

struct AccountProvisioningResult
{
    AccountProvisioningStatus status =
        AccountProvisioningStatus::InvalidLoginId;
    AccountId accountId = 0;
};

class AccountProvisioningService
{
public:
    AccountProvisioningService(
        AccountRepository& accountRepository,
        PasswordHasher& passwordHasher);

    AccountProvisioningResult CreateAccount(
        std::string loginId,
        std::string password);

private:
    AccountRepository& accountRepository_;
    PasswordHasher& passwordHasher_;
};
} // namespace dnf
