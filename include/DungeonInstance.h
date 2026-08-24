#pragma once

#include "DungeonCatalog.h"
#include "DungeonPlayerState.h"
#include "EnemyCatalog.h"
#include "PartyManager.h"
#include "PlayerId.h"
#include "RoomState.h"
#include "SessionId.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
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

struct DungeonEnemySnapshot
{
    RoomId roomId = 0;
    EnemyState enemy;
};

struct DungeonParticipantReconnect
{
    SessionId replacedSessionId = 0;
    std::chrono::steady_clock::time_point disconnectedAt;
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
    std::vector<SessionId> Participants() const;
    DungeonState State() const;

    bool HasParticipant(SessionId sessionId) const;
    bool BindParticipantPlayer(SessionId sessionId, PlayerId playerId);
    bool DisconnectParticipant(
        SessionId sessionId,
        std::chrono::steady_clock::time_point disconnectedAt);
    std::optional<DungeonParticipantReconnect> ReconnectParticipant(
        PlayerId playerId,
        SessionId newSessionId,
        std::chrono::steady_clock::time_point now,
        std::chrono::milliseconds reconnectGrace);
    bool RollbackParticipantReconnect(
        PlayerId playerId,
        SessionId newSessionId,
        const DungeonParticipantReconnect& reconnect);
    std::vector<SessionId> RemoveExpiredDisconnectedParticipants(
        std::chrono::steady_clock::time_point now,
        std::chrono::milliseconds reconnectGrace);
    std::vector<SessionId> RemoveAllParticipants();
    bool IsParticipantDisconnected(SessionId sessionId) const;
    bool HasDisconnectedParticipants() const;
    std::size_t ConnectedParticipantCount() const;
    std::chrono::steady_clock::time_point CreatedAt() const;
    std::shared_ptr<RoomState> FindRoom(RoomId roomId) const;
    std::shared_ptr<DungeonPlayerState> FindPlayer(SessionId sessionId) const;
    std::vector<DungeonEnemySnapshot> EnemySnapshots() const;
    std::size_t AdvanceRoomWaves();
    UsePortalResult TryUsePortal(SessionId sessionId);
    bool TryFinishIfCleared();
    bool Start();
    bool Finish();

private:
    DungeonId dungeonId_;
    DungeonTemplate dungeonTemplate_;
    PartyId partyId_;
    std::vector<SessionId> participants_;
    std::unordered_map<RoomId, std::shared_ptr<RoomState>> rooms_;
    std::unordered_map<SessionId, std::shared_ptr<DungeonPlayerState>> players_;
    std::unordered_map<SessionId, PlayerId> participantPlayerIds_;
    std::unordered_map<PlayerId, SessionId> playerSessions_;
    std::unordered_map<
        SessionId,
        std::chrono::steady_clock::time_point> disconnectedSince_;
    std::chrono::steady_clock::time_point createdAt_ =
        std::chrono::steady_clock::now();

    mutable std::mutex participantMutex_;
    mutable std::mutex stateMutex_;
    DungeonState state_ = DungeonState::Waiting;
};
} // namespace dnf
