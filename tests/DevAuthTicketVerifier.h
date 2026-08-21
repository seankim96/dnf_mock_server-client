#pragma once

#include "AuthTicketVerifier.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace dnf
{
class DevAuthTicketVerifier final : public AuthTicketVerifier
{
public:
    bool RegisterTicket(std::string ticket, AuthContext context);
    std::optional<AuthContext> Verify(
        const std::string& ticket) override;
    std::size_t TicketCount() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, AuthContext> tickets_;
};
} // namespace dnf
