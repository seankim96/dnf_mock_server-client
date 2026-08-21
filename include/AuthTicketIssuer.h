#pragma once

#include "AccountPlayerRepository.h"
#include "AuthTicketVerifier.h"
#include "SqliteAuthTicketStore.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace dnf
{
constexpr std::chrono::seconds DEFAULT_AUTH_TICKET_LIFETIME{60};

struct IssuedAuthTicket
{
    std::string ticket;
    std::int64_t expiresAtUnix = 0;
};

class AuthTicketIssuer
{
public:
    explicit AuthTicketIssuer(
        AccountPlayerRepository& accountPlayerRepository,
        SqliteAuthTicketStore& ticketStore,
        std::chrono::seconds ticketLifetime =
            DEFAULT_AUTH_TICKET_LIFETIME);

    std::optional<IssuedAuthTicket> Issue(
        const AuthContext& context);

private:
    std::string GenerateTicket() const;

    AccountPlayerRepository& accountPlayerRepository_;
    SqliteAuthTicketStore& ticketStore_;
    std::chrono::seconds ticketLifetime_;
};
} // namespace dnf
