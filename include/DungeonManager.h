#pragma once

#include "DungeonCatalog.h"
#include "DungeonInstance.h"
#include "PartyManager.h"
#include "PlayerId.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <unordered_map>
#include <vector>

namespace dnf
{
enum class CreateDungeonStatus
{
    Success,
    PartyNotFound,
    DungeonTemplateNotFound,
    PartyAlreadyInDungeon,
    ServerStopping
};

struct CreateDungeonResult
{
    CreateDungeonStatus status = CreateDungeonStatus::PartyNotFound;
    std::shared_ptr<DungeonInstance> dungeon;
};

enum class RegisterDungeonSessionStatus
{
    Registered,
    Reconnected,
    InvalidIdentity
};

struct RegisterDungeonSessionResult
{
    RegisterDungeonSessionStatus status =
        RegisterDungeonSessionStatus::InvalidIdentity;
    DungeonId dungeonId = 0;
    SessionId replacedSessionId = 0;
    std::optional<std::chrono::steady_clock::time_point> disconnectedAt;
};

struct AbandonedDungeonParticipant
{
    DungeonId dungeonId = 0;
    SessionId sessionId = 0;
};

struct DungeonAbandonmentSweep
{
    std::vector<AbandonedDungeonParticipant> removedParticipants;
    std::vector<DungeonId> releasedDungeons;
};

class DungeonManager
{
public:
    DungeonManager(
        PartyManager& partyManager,
        DungeonCatalog& dungeonCatalog,
        const EnemyCatalog& enemyCatalog);

    CreateDungeonResult CreateDungeon(
        PartyId partyId,
        DungeonTemplateId templateId);
    RegisterDungeonSessionResult RegisterPlayerSession(
        SessionId sessionId,
        PlayerId playerId,
        std::chrono::steady_clock::time_point now,
        std::chrono::milliseconds reconnectGrace);
    bool RollbackPlayerReconnect(
        DungeonId dungeonId,
        PlayerId playerId,
        SessionId newSessionId,
        SessionId replacedSessionId,
        std::chrono::steady_clock::time_point disconnectedAt);
    std::optional<DungeonId> DisconnectPlayerSession(
        SessionId sessionId,
        std::chrono::steady_clock::time_point disconnectedAt =
            std::chrono::steady_clock::now());
    DungeonAbandonmentSweep SweepAbandonedParticipants(
        std::chrono::steady_clock::time_point now,
        std::chrono::milliseconds reconnectGrace,
        std::chrono::milliseconds maxDungeonLifetime);
    std::shared_ptr<DungeonInstance> FindDungeon(DungeonId dungeonId) const;
    std::shared_ptr<DungeonInstance> FindDungeonByParty(PartyId partyId) const;
    std::shared_ptr<DungeonInstance> FindDungeonByParticipant(
        SessionId sessionId) const;
    std::vector<DungeonTemplate> GetDungeonTemplates() const;
    std::optional<DungeonTemplate> GetDungeonTemplate(
        DungeonTemplateId templateId) const;
    std::optional<EnemyTemplate> GetEnemyTemplate(
        EnemyTemplateId enemyTemplateId) const;

    bool StartDungeon(DungeonId dungeonId);
    bool CancelDungeon(DungeonId dungeonId);
    bool FinishDungeon(DungeonId dungeonId);
    void Stop();
    std::vector<DungeonId> WaitingDungeonIds() const;
    std::vector<DungeonId> RunningDungeonIds() const;
    std::vector<DungeonId> FinishedDungeonIds() const;
    std::size_t ActiveDungeonCount() const;

private:
    PartyManager& partyManager_;
    DungeonCatalog& dungeonCatalog_;
    const EnemyCatalog& enemyCatalog_;

    mutable std::mutex mutex_;
    DungeonId nextDungeonId_ = 1;
    std::unordered_map<DungeonId, std::shared_ptr<DungeonInstance>> dungeons_;
    std::unordered_map<PartyId, DungeonId> partyDungeons_;
    std::unordered_map<SessionId, PlayerId> authenticatedPlayers_;
    std::unordered_map<PlayerId, SessionId> activePlayerSessions_;
    bool stopping_ = false;
};
} // namespace dnf
