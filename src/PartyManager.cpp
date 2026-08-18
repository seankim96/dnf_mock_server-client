#include "PartyManager.h"

#include <algorithm>

namespace dnf
{
std::optional<PartyId> PartyManager::CreateParty(SessionId leaderSessionId)
{
    std::lock_guard lock(mutex_);

    if (joinedParties_.contains(leaderSessionId))
    {
        return std::nullopt;
    }

    const PartyId partyId = nextPartyId_++;

    PartyInfo party;
    party.id = partyId;
    party.leaderSessionId = leaderSessionId;
    party.members.push_back(leaderSessionId);

    parties_.emplace(partyId, party);
    joinedParties_.emplace(leaderSessionId, partyId);
    return partyId;
}

JoinPartyResult PartyManager::JoinParty(PartyId partyId, SessionId sessionId)
{
    std::lock_guard lock(mutex_);

    auto partyIt = parties_.find(partyId);
    if (partyIt == parties_.end())
    {
        return JoinPartyResult::PartyNotFound;
    }

    if (joinedParties_.contains(sessionId))
    {
        return JoinPartyResult::AlreadyJoined;
    }

    PartyInfo& party = partyIt->second;
    if (party.members.size() >= MAX_PARTY_MEMBERS)
    {
        return JoinPartyResult::PartyFull;
    }

    party.members.push_back(sessionId);
    joinedParties_.emplace(sessionId, partyId);
    return JoinPartyResult::Success;
}

bool PartyManager::LeaveParty(SessionId sessionId)
{
    std::lock_guard lock(mutex_);

    auto joinedIt = joinedParties_.find(sessionId);
    if (joinedIt == joinedParties_.end())
    {
        return false;
    }

    auto partyIt = parties_.find(joinedIt->second);
    if (partyIt == parties_.end())
    {
        joinedParties_.erase(joinedIt);
        return false;
    }

    PartyInfo& party = partyIt->second;
    party.members.erase(
        std::remove(party.members.begin(), party.members.end(), sessionId),
        party.members.end());
    joinedParties_.erase(joinedIt);

    if (party.members.empty())
    {
        parties_.erase(partyIt);
        return true;
    }

    if (party.leaderSessionId == sessionId)
    {
        party.leaderSessionId = party.members.front();
    }

    return true;
}

std::optional<PartyInfo> PartyManager::GetParty(PartyId partyId) const
{
    std::lock_guard lock(mutex_);

    auto partyIt = parties_.find(partyId);
    if (partyIt == parties_.end())
    {
        return std::nullopt;
    }

    return partyIt->second;
}

std::optional<PartyId> PartyManager::GetJoinedParty(SessionId sessionId) const
{
    std::lock_guard lock(mutex_);

    auto joinedIt = joinedParties_.find(sessionId);
    if (joinedIt == joinedParties_.end())
    {
        return std::nullopt;
    }

    return joinedIt->second;
}
} // namespace dnf
