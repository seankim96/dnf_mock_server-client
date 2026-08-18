#pragma once

#include "SessionId.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace dnf
{
using PartyId = std::uint64_t;

constexpr std::size_t MAX_PARTY_MEMBERS = 4;

struct PartyInfo
{
    PartyId id = 0;
    SessionId leaderSessionId = 0;
    std::vector<SessionId> members;
};

enum class JoinPartyResult
{
    Success,
    PartyNotFound,
    PartyFull,
    AlreadyJoined
};

class PartyManager
{
public:
    std::optional<PartyId> CreateParty(SessionId leaderSessionId);
    JoinPartyResult JoinParty(PartyId partyId, SessionId sessionId);
    bool LeaveParty(SessionId sessionId);

    std::optional<PartyInfo> GetParty(PartyId partyId) const;
    std::optional<PartyId> GetJoinedParty(SessionId sessionId) const;

private:
    mutable std::mutex mutex_;
    PartyId nextPartyId_ = 1;
    std::unordered_map<PartyId, PartyInfo> parties_;
    std::unordered_map<SessionId, PartyId> joinedParties_;
};
} // namespace dnf
