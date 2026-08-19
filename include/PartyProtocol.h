#pragma once

#include "PartyManager.h"

#include <cstdint>
#include <vector>

namespace dnf
{
enum class CreatePartyResult : std::uint8_t
{
    Success,
    AlreadyInParty
};

struct CreatePartyResponseData
{
    CreatePartyResult result = CreatePartyResult::Success;
    PartyId partyId = 0;
    SessionId leaderSessionId = 0;
};

void ValidateCreatePartyRequestPayload(
    const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> EncodeCreatePartyResponsePayload(
    CreatePartyResult result,
    PartyId partyId,
    SessionId leaderSessionId);

CreatePartyResponseData DecodeCreatePartyResponsePayload(
    const std::vector<std::uint8_t>& payload);
} // namespace dnf
