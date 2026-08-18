#include "DungeonInstance.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace dnf
{
DungeonInstance::DungeonInstance(
    DungeonId dungeonId,
    DungeonTemplateId templateId,
    PartyId partyId,
    std::vector<SessionId> participants)
    : dungeonId_(dungeonId),
      templateId_(templateId),
      partyId_(partyId),
      participants_(std::move(participants))
{
    if (dungeonId_ == 0 || templateId_ == 0 || partyId_ == 0)
    {
        throw std::invalid_argument("Dungeon IDs must not be zero");
    }

    if (participants_.empty() || participants_.size() > MAX_PARTY_MEMBERS)
    {
        throw std::invalid_argument("Invalid dungeon participant count");
    }
}

DungeonId DungeonInstance::Id() const
{
    return dungeonId_;
}

DungeonTemplateId DungeonInstance::TemplateId() const
{
    return templateId_;
}

PartyId DungeonInstance::Party() const
{
    return partyId_;
}

const std::vector<SessionId>& DungeonInstance::Participants() const
{
    return participants_;
}

DungeonState DungeonInstance::State() const
{
    std::lock_guard lock(stateMutex_);
    return state_;
}

bool DungeonInstance::HasParticipant(SessionId sessionId) const
{
    return std::find(
               participants_.begin(),
               participants_.end(),
               sessionId) != participants_.end();
}

bool DungeonInstance::Start()
{
    std::lock_guard lock(stateMutex_);

    if (state_ != DungeonState::Waiting)
    {
        return false;
    }

    state_ = DungeonState::Running;
    return true;
}

bool DungeonInstance::Finish()
{
    std::lock_guard lock(stateMutex_);

    if (state_ != DungeonState::Running)
    {
        return false;
    }

    state_ = DungeonState::Finished;
    return true;
}
} // namespace dnf
