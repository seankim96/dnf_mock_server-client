#include "PartyProtocol.h"

#include <stdexcept>

namespace dnf
{
namespace
{
constexpr std::size_t CREATE_PARTY_RESPONSE_SIZE = 17;

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

bool IsValidResponse(
    CreatePartyResult result,
    PartyId partyId,
    SessionId leaderSessionId)
{
    if (result < CreatePartyResult::Success ||
        result > CreatePartyResult::AlreadyInParty)
    {
        return false;
    }

    const bool succeeded = result == CreatePartyResult::Success;
    return succeeded == (partyId != 0) &&
           succeeded == (leaderSessionId != 0);
}
} // namespace

void ValidateCreatePartyRequestPayload(
    const std::vector<std::uint8_t>& payload)
{
    if (!payload.empty())
    {
        throw std::runtime_error(
            "Create party request payload must be empty");
    }
}

std::vector<std::uint8_t> EncodeCreatePartyResponsePayload(
    CreatePartyResult result,
    PartyId partyId,
    SessionId leaderSessionId)
{
    if (!IsValidResponse(result, partyId, leaderSessionId))
    {
        throw std::invalid_argument("Invalid create party response");
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(CREATE_PARTY_RESPONSE_SIZE);
    payload.push_back(static_cast<std::uint8_t>(result));
    AppendUint64(payload, partyId);
    AppendUint64(payload, leaderSessionId);
    return payload;
}

CreatePartyResponseData DecodeCreatePartyResponsePayload(
    const std::vector<std::uint8_t>& payload)
{
    if (payload.size() != CREATE_PARTY_RESPONSE_SIZE)
    {
        throw std::runtime_error(
            "Invalid create party response payload size");
    }

    const auto result = static_cast<CreatePartyResult>(payload[0]);
    const PartyId partyId = ReadUint64(payload, 1);
    const SessionId leaderSessionId = ReadUint64(payload, 9);

    if (!IsValidResponse(result, partyId, leaderSessionId))
    {
        throw std::runtime_error("Invalid create party response data");
    }

    return {result, partyId, leaderSessionId};
}
} // namespace dnf
