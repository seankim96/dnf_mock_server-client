#pragma once

#include "AuthTicketVerifier.h"
#include "SqliteAuthTicketStore.h"

namespace dnf
{
class SqliteAuthTicketVerifier final : public AuthTicketVerifier
{
public:
    explicit SqliteAuthTicketVerifier(SqliteAuthTicketStore& ticketStore);

    std::optional<AuthContext> Verify(
        const std::string& ticket) override;

private:
    SqliteAuthTicketStore& ticketStore_;
};
} // namespace dnf
