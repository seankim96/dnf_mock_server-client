#pragma once

#include "AccountId.h"
#include "PlayerId.h"

#include <cstddef>
#include <optional>
#include <string>

namespace dnf
{
constexpr std::size_t MAX_AUTH_TICKET_LENGTH = 256;

struct AuthContext
{
    AccountId accountId = 0;
    PlayerId playerId = 0;
};

bool IsValidAuthContext(const AuthContext& context);
bool IsValidAuthTicket(const std::string& ticket);

class AuthTicketVerifier
{
public:
    virtual ~AuthTicketVerifier() = default;

    virtual std::optional<AuthContext> Verify(
        const std::string& ticket) = 0;
};
} // namespace dnf
