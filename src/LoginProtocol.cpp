#include "LoginProtocol.h"

#include <stdexcept>

namespace dnf
{
namespace
{
constexpr std::size_t LOGIN_RESPONSE_SIZE = 9;

bool IsValidResult(LoginResult result)
{
    return result >= LoginSuccess &&
           result <= InvalidPlayerNameCharacter;
}

bool IsValidResponse(LoginResult result, SessionId sessionId)
{
    if (!IsValidResult(result))
    {
        return false;
    }

    const bool succeeded = result == LoginSuccess;
    return succeeded == (sessionId != 0);
}

void AppendUint64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

std::uint64_t ReadUint64(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset)
{
    std::uint64_t value = 0;

    for (std::size_t index = 0; index < 8; ++index)
    {
        value = (value << 8) | bytes[offset + index];
    }

    return value;
}
} // namespace

std::vector<std::uint8_t> EncodeLoginResponsePayload(
    LoginResult result,
    SessionId sessionId)
{
    if (!IsValidResponse(result, sessionId))
    {
        throw std::invalid_argument("Invalid login response");
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(LOGIN_RESPONSE_SIZE);
    payload.push_back(static_cast<std::uint8_t>(result));
    AppendUint64(payload, sessionId);
    return payload;
}

LoginResponseData DecodeLoginResponsePayload(
    const std::vector<std::uint8_t>& payload)
{
    if (payload.size() != LOGIN_RESPONSE_SIZE)
    {
        throw std::runtime_error("Invalid login response payload size");
    }

    const auto result = static_cast<LoginResult>(payload[0]);
    const SessionId sessionId = ReadUint64(payload, 1);

    if (!IsValidResponse(result, sessionId))
    {
        throw std::runtime_error("Invalid login response data");
    }

    return {result, sessionId};
}
} // namespace dnf
