#include "DevAuthTicketVerifier.h"

#include <utility>

namespace dnf
{
bool DevAuthTicketVerifier::RegisterTicket(
    std::string ticket,
    AuthContext context)
{
    if (!IsValidAuthTicket(ticket) || !IsValidAuthContext(context))
    {
        return false;
    }

    std::lock_guard lock(mutex_);
    return tickets_.emplace(std::move(ticket), context).second;
}

std::optional<AuthContext> DevAuthTicketVerifier::Verify(
    const std::string& ticket)
{
    if (!IsValidAuthTicket(ticket))
    {
        return std::nullopt;
    }

    std::lock_guard lock(mutex_);

    const auto ticketIt = tickets_.find(ticket);
    if (ticketIt == tickets_.end())
    {
        return std::nullopt;
    }

    const AuthContext context = ticketIt->second;
    tickets_.erase(ticketIt);
    return context;
}

std::size_t DevAuthTicketVerifier::TicketCount() const
{
    std::lock_guard lock(mutex_);
    return tickets_.size();
}
} // namespace dnf
