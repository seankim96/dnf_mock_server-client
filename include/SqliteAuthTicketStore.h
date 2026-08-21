#pragma once

#include "AuthTicketVerifier.h"
#include "SqliteDatabase.h"

#include <cstdint>
#include <optional>
#include <string>

namespace dnf
{
class SqliteAuthTicketStore
{
public:
    explicit SqliteAuthTicketStore(SqliteDatabase& database);

    bool IssueTicket(
        const std::string& ticket,
        const AuthContext& context,
        std::int64_t expiresAtUnix);

    std::optional<AuthContext> ConsumeTicket(
        const std::string& ticket,
        std::int64_t nowUnix);

private:
    SqliteDatabase& database_;
};
} // namespace dnf
