#pragma once

#include "AccountId.h"

#include <cstddef>
#include <string>

namespace dnf
{
constexpr std::size_t MIN_ACCOUNT_LOGIN_ID_LENGTH = 4;
constexpr std::size_t MAX_ACCOUNT_LOGIN_ID_LENGTH = 32;
constexpr std::size_t MAX_ENCODED_PASSWORD_HASH_LENGTH = 256;

struct Account
{
    AccountId accountId = 0;
    std::string loginId;
    std::string encodedPasswordHash;
};

bool IsValidAccountLoginId(const std::string& loginId);
bool IsValidEncodedPasswordHash(
    const std::string& encodedPasswordHash);
bool IsValidAccount(const Account& account);
} // namespace dnf
