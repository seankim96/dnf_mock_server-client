#pragma once

#include "Account.h"

#include <optional>
#include <string>

namespace dnf
{
// 이 인터페이스의 함수는 DatabaseExecutor 작업 스레드에서 호출한다.
class AccountRepository
{
public:
    virtual ~AccountRepository() = default;

    virtual std::optional<Account> FindAccount(
        AccountId accountId) = 0;
    virtual std::optional<Account> FindAccountByLoginId(
        const std::string& loginId) = 0;
    virtual std::optional<Account> CreateAccount(
        const std::string& loginId,
        const std::string& encodedPasswordHash) = 0;
};
} // namespace dnf
