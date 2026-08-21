#include "Account.h"

#include <algorithm>

namespace dnf
{
bool IsValidAccountLoginId(const std::string& loginId)
{
    if (loginId.size() < MIN_ACCOUNT_LOGIN_ID_LENGTH ||
        loginId.size() > MAX_ACCOUNT_LOGIN_ID_LENGTH)
    {
        return false;
    }

    return std::all_of(
        loginId.begin(),
        loginId.end(),
        [](char character)
        {
            const bool isLowercaseLetter =
                character >= 'a' && character <= 'z';
            const bool isUppercaseLetter =
                character >= 'A' && character <= 'Z';
            const bool isDigit =
                character >= '0' && character <= '9';

            return isLowercaseLetter ||
                   isUppercaseLetter ||
                   isDigit ||
                   character == '_';
        });
}

bool IsValidEncodedPasswordHash(
    const std::string& encodedPasswordHash)
{
    return !encodedPasswordHash.empty() &&
           encodedPasswordHash.size() <=
               MAX_ENCODED_PASSWORD_HASH_LENGTH;
}

bool IsValidAccount(const Account& account)
{
    return account.accountId != 0 &&
           IsValidAccountLoginId(account.loginId) &&
           IsValidEncodedPasswordHash(account.encodedPasswordHash);
}
} // namespace dnf
