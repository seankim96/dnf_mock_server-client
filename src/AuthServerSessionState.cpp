#include "AuthServerSessionState.h"

namespace dnf
{
bool AuthServerSessionState::MarkAuthenticated(AccountId accountId)
{
    if (accountId == 0)
    {
        return false;
    }

    std::lock_guard lock(mutex_);
    if (accountId_.has_value())
    {
        return false;
    }

    accountId_ = accountId;
    return true;
}

bool AuthServerSessionState::IsAuthenticated() const
{
    std::lock_guard lock(mutex_);
    return accountId_.has_value();
}

std::optional<AccountId>
AuthServerSessionState::AuthenticatedAccount() const
{
    std::lock_guard lock(mutex_);
    return accountId_;
}
} // namespace dnf
