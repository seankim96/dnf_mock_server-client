#include "DungeonManager.h"

namespace dnf
{
DungeonManager::DungeonManager(
    PartyManager& partyManager,
    DungeonCatalog& dungeonCatalog,
    const EnemyCatalog& enemyCatalog)
    : partyManager_(partyManager),
      dungeonCatalog_(dungeonCatalog),
      enemyCatalog_(enemyCatalog)
{
}

CreateDungeonResult DungeonManager::CreateDungeon(
    PartyId partyId,
    DungeonTemplateId templateId)
{
    {
        std::lock_guard lock(mutex_);
        if (stopping_)
        {
            return {CreateDungeonStatus::ServerStopping, nullptr};
        }
    }

    const auto party = partyManager_.GetParty(partyId);
    if (!party.has_value())
    {
        return {CreateDungeonStatus::PartyNotFound, nullptr};
    }

    const auto dungeonTemplate = dungeonCatalog_.GetDungeon(templateId);
    if (!dungeonTemplate.has_value())
    {
        return {CreateDungeonStatus::DungeonTemplateNotFound, nullptr};
    }

    std::lock_guard lock(mutex_);

    if (stopping_)
    {
        return {CreateDungeonStatus::ServerStopping, nullptr};
    }

    if (partyDungeons_.contains(partyId))
    {
        return {CreateDungeonStatus::PartyAlreadyInDungeon, nullptr};
    }

    const DungeonId dungeonId = nextDungeonId_++;
    auto dungeon = std::make_shared<DungeonInstance>(
        dungeonId,
        dungeonTemplate.value(),
        partyId,
        party->members,
        enemyCatalog_);

    for (SessionId participantSessionId : party->members)
    {
        const auto playerIt =
            authenticatedPlayers_.find(participantSessionId);
        if (playerIt != authenticatedPlayers_.end())
        {
            dungeon->BindParticipantPlayer(
                participantSessionId,
                playerIt->second);
        }
    }

    dungeons_.emplace(dungeonId, dungeon);
    partyDungeons_.emplace(partyId, dungeonId);

    return {CreateDungeonStatus::Success, dungeon};
}

RegisterDungeonSessionResult DungeonManager::RegisterPlayerSession(
    SessionId sessionId,
    PlayerId playerId,
    std::chrono::steady_clock::time_point now,
    std::chrono::milliseconds reconnectGrace)
{
    if (sessionId == 0 || playerId == 0 ||
        reconnectGrace <= std::chrono::milliseconds::zero())
    {
        return {};
    }

    std::lock_guard lock(mutex_);
    if (stopping_)
    {
        return {};
    }

    const auto authenticatedIt = authenticatedPlayers_.find(sessionId);
    if (authenticatedIt != authenticatedPlayers_.end() &&
        authenticatedIt->second != playerId)
    {
        return {};
    }

    const auto activeSessionIt = activePlayerSessions_.find(playerId);
    if (activeSessionIt != activePlayerSessions_.end() &&
        activeSessionIt->second != sessionId)
    {
        return {};
    }

    authenticatedPlayers_.try_emplace(sessionId, playerId);
    activePlayerSessions_.try_emplace(playerId, sessionId);

    for (const auto& [dungeonId, dungeon] : dungeons_)
    {
        if (dungeon->HasParticipant(sessionId))
        {
            if (!dungeon->BindParticipantPlayer(sessionId, playerId))
            {
                authenticatedPlayers_.erase(sessionId);
                activePlayerSessions_.erase(playerId);
                return {};
            }

            return {
                RegisterDungeonSessionStatus::Registered,
                dungeonId,
                0,
                std::nullopt};
        }
    }

    for (const auto& [dungeonId, dungeon] : dungeons_)
    {
        if (dungeon->State() == DungeonState::Finished)
        {
            continue;
        }

        const auto reconnect =
            dungeon->ReconnectParticipant(
                playerId,
                sessionId,
                now,
                reconnectGrace);
        if (!reconnect.has_value())
        {
            continue;
        }

        authenticatedPlayers_.erase(
            reconnect->replacedSessionId);
        return {
            RegisterDungeonSessionStatus::Reconnected,
            dungeonId,
            reconnect->replacedSessionId,
            reconnect->disconnectedAt};
    }

    return {
        RegisterDungeonSessionStatus::Registered,
        0,
        0,
        std::nullopt};
}

bool DungeonManager::RollbackPlayerReconnect(
    DungeonId dungeonId,
    PlayerId playerId,
    SessionId newSessionId,
    SessionId replacedSessionId,
    std::chrono::steady_clock::time_point disconnectedAt)
{
    std::lock_guard lock(mutex_);

    const auto dungeonIt = dungeons_.find(dungeonId);
    if (dungeonIt == dungeons_.end())
    {
        return false;
    }

    const bool rolledBack =
        dungeonIt->second->RollbackParticipantReconnect(
            playerId,
            newSessionId,
            {replacedSessionId, disconnectedAt});
    if (!rolledBack)
    {
        return false;
    }

    authenticatedPlayers_.erase(newSessionId);
    const auto activeSessionIt = activePlayerSessions_.find(playerId);
    if (activeSessionIt != activePlayerSessions_.end() &&
        activeSessionIt->second == newSessionId)
    {
        activePlayerSessions_.erase(activeSessionIt);
    }

    return true;
}

std::optional<DungeonId> DungeonManager::DisconnectPlayerSession(
    SessionId sessionId,
    std::chrono::steady_clock::time_point disconnectedAt)
{
    if (sessionId == 0)
    {
        return std::nullopt;
    }

    std::lock_guard lock(mutex_);
    const auto authenticatedIt = authenticatedPlayers_.find(sessionId);
    if (authenticatedIt != authenticatedPlayers_.end())
    {
        const PlayerId playerId = authenticatedIt->second;
        const auto activeSessionIt = activePlayerSessions_.find(playerId);
        if (activeSessionIt != activePlayerSessions_.end() &&
            activeSessionIt->second == sessionId)
        {
            activePlayerSessions_.erase(activeSessionIt);
        }

        authenticatedPlayers_.erase(authenticatedIt);
    }

    for (const auto& [dungeonId, dungeon] : dungeons_)
    {
        if (dungeon->DisconnectParticipant(sessionId, disconnectedAt))
        {
            return dungeonId;
        }
    }

    return std::nullopt;
}

DungeonAbandonmentSweep DungeonManager::SweepAbandonedParticipants(
    std::chrono::steady_clock::time_point now,
    std::chrono::milliseconds reconnectGrace,
    std::chrono::milliseconds maxDungeonLifetime)
{
    DungeonAbandonmentSweep sweep;
    if (reconnectGrace <= std::chrono::milliseconds::zero() ||
        maxDungeonLifetime <= std::chrono::milliseconds::zero())
    {
        return sweep;
    }

    std::lock_guard lock(mutex_);

    for (auto dungeonIt = dungeons_.begin();
         dungeonIt != dungeons_.end();)
    {
        const DungeonId dungeonId = dungeonIt->first;
        const auto& dungeon = dungeonIt->second;
        const bool lifetimeExpired =
            now >= dungeon->CreatedAt() &&
            now - dungeon->CreatedAt() >= maxDungeonLifetime;

        std::vector<SessionId> removedSessions = lifetimeExpired
            ? dungeon->RemoveAllParticipants()
            : dungeon->RemoveExpiredDisconnectedParticipants(
                  now,
                  reconnectGrace);

        for (SessionId removedSessionId : removedSessions)
        {
            sweep.removedParticipants.push_back(
                {dungeonId, removedSessionId});
        }

        if (!lifetimeExpired && !dungeon->Participants().empty())
        {
            ++dungeonIt;
            continue;
        }

        partyDungeons_.erase(dungeon->Party());
        sweep.releasedDungeons.push_back(dungeonId);
        dungeonIt = dungeons_.erase(dungeonIt);
    }

    return sweep;
}

std::shared_ptr<DungeonInstance> DungeonManager::FindDungeon(
    DungeonId dungeonId) const
{
    std::lock_guard lock(mutex_);

    auto dungeonIt = dungeons_.find(dungeonId);
    if (dungeonIt == dungeons_.end())
    {
        return nullptr;
    }

    return dungeonIt->second;
}

std::shared_ptr<DungeonInstance> DungeonManager::FindDungeonByParty(
    PartyId partyId) const
{
    std::lock_guard lock(mutex_);

    auto partyIt = partyDungeons_.find(partyId);
    if (partyIt == partyDungeons_.end())
    {
        return nullptr;
    }

    return dungeons_.at(partyIt->second);
}

std::shared_ptr<DungeonInstance> DungeonManager::FindDungeonByParticipant(
    SessionId sessionId) const
{
    std::lock_guard lock(mutex_);

    for (const auto& [dungeonId, dungeon] : dungeons_)
    {
        (void)dungeonId;
        if (dungeon->HasParticipant(sessionId))
        {
            return dungeon;
        }
    }

    return nullptr;
}

std::vector<DungeonTemplate> DungeonManager::GetDungeonTemplates() const
{
    return dungeonCatalog_.GetDungeonList();
}

std::optional<DungeonTemplate> DungeonManager::GetDungeonTemplate(
    DungeonTemplateId templateId) const
{
    return dungeonCatalog_.GetDungeon(templateId);
}

std::optional<EnemyTemplate> DungeonManager::GetEnemyTemplate(
    EnemyTemplateId enemyTemplateId) const
{
    return enemyCatalog_.GetEnemy(enemyTemplateId);
}

bool DungeonManager::StartDungeon(DungeonId dungeonId)
{
    std::lock_guard lock(mutex_);

    auto dungeonIt = dungeons_.find(dungeonId);
    if (dungeonIt == dungeons_.end())
    {
        return false;
    }

    return dungeonIt->second->Start();
}

bool DungeonManager::CancelDungeon(DungeonId dungeonId)
{
    std::lock_guard lock(mutex_);

    auto dungeonIt = dungeons_.find(dungeonId);
    if (dungeonIt == dungeons_.end() ||
        dungeonIt->second->State() != DungeonState::Waiting)
    {
        return false;
    }

    partyDungeons_.erase(dungeonIt->second->Party());
    dungeons_.erase(dungeonIt);
    return true;
}

bool DungeonManager::FinishDungeon(DungeonId dungeonId)
{
    std::lock_guard lock(mutex_);

    auto dungeonIt = dungeons_.find(dungeonId);
    if (dungeonIt == dungeons_.end())
    {
        return false;
    }

    if (dungeonIt->second->State() == DungeonState::Running &&
        !dungeonIt->second->Finish())
    {
        return false;
    }

    if (dungeonIt->second->State() != DungeonState::Finished)
    {
        return false;
    }

    partyDungeons_.erase(dungeonIt->second->Party());
    dungeons_.erase(dungeonIt);
    return true;
}

void DungeonManager::Stop()
{
    std::lock_guard lock(mutex_);
    if (stopping_)
    {
        return;
    }

    stopping_ = true;
    partyDungeons_.clear();
    authenticatedPlayers_.clear();
    activePlayerSessions_.clear();
    dungeons_.clear();
}

std::vector<DungeonId> DungeonManager::WaitingDungeonIds() const
{
    std::lock_guard lock(mutex_);

    std::vector<DungeonId> dungeonIds;
    dungeonIds.reserve(dungeons_.size());

    for (const auto& [dungeonId, dungeon] : dungeons_)
    {
        if (dungeon->State() == DungeonState::Waiting)
        {
            dungeonIds.push_back(dungeonId);
        }
    }

    return dungeonIds;
}

std::vector<DungeonId> DungeonManager::RunningDungeonIds() const
{
    std::lock_guard lock(mutex_);

    std::vector<DungeonId> dungeonIds;
    dungeonIds.reserve(dungeons_.size());

    for (const auto& [dungeonId, dungeon] : dungeons_)
    {
        if (dungeon->State() == DungeonState::Running)
        {
            dungeonIds.push_back(dungeonId);
        }
    }

    return dungeonIds;
}

std::vector<DungeonId> DungeonManager::FinishedDungeonIds() const
{
    std::lock_guard lock(mutex_);

    std::vector<DungeonId> dungeonIds;
    dungeonIds.reserve(dungeons_.size());

    for (const auto& [dungeonId, dungeon] : dungeons_)
    {
        if (dungeon->State() == DungeonState::Finished)
        {
            dungeonIds.push_back(dungeonId);
        }
    }

    return dungeonIds;
}

std::size_t DungeonManager::ActiveDungeonCount() const
{
    std::lock_guard lock(mutex_);
    return dungeons_.size();
}
} // namespace dnf
