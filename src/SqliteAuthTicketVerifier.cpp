#include "SqliteAuthTicketVerifier.h"

#include <chrono>
#include <cstdint>

namespace dnf
{
SqliteAuthTicketVerifier::SqliteAuthTicketVerifier(
    SqliteAuthTicketStore& ticketStore)
    : ticketStore_(ticketStore)
{
}

std::optional<AuthContext> SqliteAuthTicketVerifier::Verify(
    const std::string& ticket)
{
    const auto now = std::chrono::system_clock::now();
    const auto unixSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();

    return ticketStore_.ConsumeTicket(
        ticket,
        static_cast<std::int64_t>(unixSeconds));
}
} // namespace dnf
