#include "AuthTicketVerifier.h"

namespace dnf
{
bool IsValidAuthContext(const AuthContext& context)
{
    return context.accountId != 0 && context.playerId != 0;
}

bool IsValidAuthTicket(const std::string& ticket)
{
    return !ticket.empty() && ticket.size() <= MAX_AUTH_TICKET_LENGTH;
}
} // namespace dnf
