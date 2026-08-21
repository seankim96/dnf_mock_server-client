#pragma once

#include "AuthTicketVerifier.h"
#include "PlayerProfile.h"

#include <mutex>
#include <optional>

namespace dnf
{
struct SessionAuthSnapshot
{
    AuthContext authContext;
    PlayerProfile profile;
};

class SessionAuthState
{
public:
    bool Authenticate(
        const AuthContext& authContext,
        const PlayerProfile& profile);

    bool IsAuthenticated() const;
    std::optional<SessionAuthSnapshot> Snapshot() const;

private:
    mutable std::mutex mutex_;
    std::optional<SessionAuthSnapshot> snapshot_;
};
} // namespace dnf
