#pragma once

#include "Account.h"

#include <optional>
#include <string>

namespace dnf
{
// 서버의 비동기 경로에서는 DatabaseExecutor 작업 스레드에서 호출한다.
// 단일 작업 관리 도구에서는 같은 인터페이스를 동기 호출할 수 있다.
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
