#include "SessionAuthState.h"

namespace dnf
{
bool SessionAuthState::Authenticate(
    const AuthContext& authContext,
    const PlayerProfile& profile)
{
    if (!IsValidAuthContext(authContext) ||
        !IsValidPlayerProfile(profile) ||
        authContext.playerId != profile.playerId)
    {
        return false;
    }

    std::lock_guard lock(mutex_);
    if (snapshot_.has_value())
    {
        return false;
    }

    snapshot_ = SessionAuthSnapshot{authContext, profile};
    return true;
}

bool SessionAuthState::IsAuthenticated() const
{
    std::lock_guard lock(mutex_);
    return snapshot_.has_value();
}

std::optional<SessionAuthSnapshot> SessionAuthState::Snapshot() const
{
    std::lock_guard lock(mutex_);
    return snapshot_;
}
} // namespace dnf
