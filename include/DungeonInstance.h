#pragma once

#include "DungeonCatalog.h"
#include "DungeonPlayerState.h"
#include "EnemyCatalog.h"
#include "PartyManager.h"
#include "RoomState.h"
#include "SessionId.h"

#include <cstdint>
#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_map>
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

enum class UsePortalResult
{
    Success,
    DungeonNotRunning,
    PlayerNotFound,
    NotInsidePortal,
    RoomNotCleared,
    TargetRoomNotFound,
    TargetPositionBlocked
};

class DungeonInstance
{
public:
    DungeonInstance(
        DungeonId dungeonId,
        DungeonTemplate dungeonTemplate,
        PartyId partyId,
        std::vector<SessionId> participants,
        const EnemyCatalog& enemyCatalog);

    DungeonId Id() const;
    DungeonTemplateId TemplateId() const;
    PartyId Party() const;
    const std::vector<SessionId>& Participants() const;
    DungeonState State() const;

    bool HasParticipant(SessionId sessionId) const;
    std::shared_ptr<RoomState> FindRoom(RoomId roomId) const;
    std::shared_ptr<DungeonPlayerState> FindPlayer(SessionId sessionId) const;
    std::size_t AdvanceRoomWaves();
    UsePortalResult TryUsePortal(SessionId sessionId);
    bool Start();
    bool Finish();

private:
    DungeonId dungeonId_;
    DungeonTemplate dungeonTemplate_;
    PartyId partyId_;
    std::vector<SessionId> participants_;
    std::unordered_map<RoomId, std::shared_ptr<RoomState>> rooms_;
    std::unordered_map<SessionId, std::shared_ptr<DungeonPlayerState>> players_;

    mutable std::mutex stateMutex_;
    DungeonState state_ = DungeonState::Waiting;
};
} // namespace dnf
