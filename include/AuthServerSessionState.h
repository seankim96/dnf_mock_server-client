#pragma once

#include "AccountId.h"

#include <mutex>
#include <optional>

namespace dnf
{
class AuthServerSessionState
{
public:
    // 비밀번호 검증이 성공한 뒤 호출한다.
    bool MarkAuthenticated(AccountId accountId);

    bool IsAuthenticated() const;
    std::optional<AccountId> AuthenticatedAccount() const;

private:
    mutable std::mutex mutex_;
    std::optional<AccountId> accountId_;
};
} // namespace dnf
