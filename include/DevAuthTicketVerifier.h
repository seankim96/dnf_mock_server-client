#pragma once

#include "AuthTicketVerifier.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace dnf
{
// 로컬 시연용 구현이다. 등록된 티켓은 검증 성공 시 즉시 제거한다.
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
