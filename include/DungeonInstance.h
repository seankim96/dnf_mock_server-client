#pragma once

#include "PartyManager.h"
#include "SessionId.h"

#include <cstdint>
#include <mutex>
#include <vector>

namespace dnf
{
using DungeonId = std::uint64_t;

enum class DungeonState
{
    Waiting,
    Running,
    Finished
};

class DungeonInstance
{
public:
    DungeonInstance(
        DungeonId dungeonId,
        PartyId partyId,
        std::vector<SessionId> participants);

    DungeonId Id() const;
    PartyId Party() const;
    const std::vector<SessionId>& Participants() const;
    DungeonState State() const;

    bool HasParticipant(SessionId sessionId) const;
    bool Start();
    bool Finish();

private:
    DungeonId dungeonId_;
    PartyId partyId_;
    std::vector<SessionId> participants_;

    mutable std::mutex stateMutex_;
    DungeonState state_ = DungeonState::Waiting;
};
} // namespace dnf
