#include "AuthTicketIssuer.h"

#include <openssl/rand.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace dnf
{
namespace
{
constexpr std::size_t TICKET_BYTE_COUNT = 32;
constexpr char HEX_DIGITS[] = "0123456789abcdef";

std::int64_t CurrentUnixTime()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
} // namespace

AuthTicketIssuer::AuthTicketIssuer(
    SqliteAuthTicketStore& ticketStore,
    std::chrono::seconds ticketLifetime)
    : ticketStore_(ticketStore),
      ticketLifetime_(ticketLifetime)
{
    if (ticketLifetime_.count() <= 0)
    {
        throw std::invalid_argument(
            "Auth ticket lifetime must be positive");
    }
}

std::optional<IssuedAuthTicket> AuthTicketIssuer::Issue(
    const AuthContext& context)
{
    if (!IsValidAuthContext(context))
    {
        return std::nullopt;
    }

    const std::int64_t nowUnix = CurrentUnixTime();
    if (ticketLifetime_.count() >
        std::numeric_limits<std::int64_t>::max() - nowUnix)
    {
        throw std::overflow_error("Auth ticket expiry time overflowed");
    }

    IssuedAuthTicket issuedTicket;
    issuedTicket.ticket = GenerateTicket();
    issuedTicket.expiresAtUnix = nowUnix + ticketLifetime_.count();

    if (!ticketStore_.IssueTicket(
            issuedTicket.ticket,
            context,
            issuedTicket.expiresAtUnix))
    {
        return std::nullopt;
    }

    return issuedTicket;
}

std::string AuthTicketIssuer::GenerateTicket() const
{
    std::array<unsigned char, TICKET_BYTE_COUNT> randomBytes{};
    if (RAND_bytes(
            randomBytes.data(),
            static_cast<int>(randomBytes.size())) != 1)
    {
        throw std::runtime_error("Failed to generate an auth ticket");
    }

    std::string ticket;
    ticket.reserve(randomBytes.size() * 2);

    for (const unsigned char byte : randomBytes)
    {
        ticket.push_back(HEX_DIGITS[byte >> 4]);
        ticket.push_back(HEX_DIGITS[byte & 0x0F]);
    }

    return ticket;
}
} // namespace dnf
