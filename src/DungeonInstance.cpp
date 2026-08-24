#include "DungeonInstance.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace dnf
{
DungeonInstance::DungeonInstance(
    DungeonId dungeonId,
    DungeonTemplate dungeonTemplate,
    PartyId partyId,
    std::vector<SessionId> participants,
    const EnemyCatalog& enemyCatalog)
    : dungeonId_(dungeonId),
      dungeonTemplate_(std::move(dungeonTemplate)),
      partyId_(partyId),
      participants_(std::move(participants))
{
    if (dungeonId_ == 0 || dungeonTemplate_.id == 0 || partyId_ == 0)
    {
        throw std::invalid_argument("Dungeon IDs must not be zero");
    }

    if (dungeonTemplate_.rooms.empty() ||
        participants_.empty() ||
        participants_.size() > MAX_PARTY_MEMBERS)
    {
        throw std::invalid_argument("Invalid dungeon contents");
    }

    std::unordered_set<SessionId> participantIds;
    for (SessionId sessionId : participants_)
    {
        if (sessionId == 0 || !participantIds.insert(sessionId).second)
        {
            throw std::invalid_argument("Invalid dungeon participant");
        }
    }

    for (const RoomTemplate& roomTemplate : dungeonTemplate_.rooms)
    {
        const bool inserted = rooms_
                                  .emplace(
                                      roomTemplate.id,
                                      std::make_shared<RoomState>(
                                          roomTemplate,
                                          enemyCatalog))
                                  .second;

        if (!inserted)
        {
            throw std::invalid_argument("Duplicate dungeon room ID");
        }
    }

    const RoomTemplate& firstRoom = dungeonTemplate_.rooms.front();
    for (SessionId sessionId : participants_)
    {
        players_.emplace(
            sessionId,
            std::make_shared<DungeonPlayerState>(
                sessionId,
                firstRoom.id,
                firstRoom.playerSpawn));
    }
}

DungeonId DungeonInstance::Id() const
{
    return dungeonId_;
}

DungeonTemplateId DungeonInstance::TemplateId() const
{
    return dungeonTemplate_.id;
}

PartyId DungeonInstance::Party() const
{
    return partyId_;
}

std::vector<SessionId> DungeonInstance::Participants() const
{
    std::lock_guard lock(participantMutex_);
    return participants_;
}

DungeonState DungeonInstance::State() const
{
    std::lock_guard lock(stateMutex_);
    return state_;
}

bool DungeonInstance::HasParticipant(SessionId sessionId) const
{
    std::lock_guard lock(participantMutex_);
    return players_.contains(sessionId);
}

bool DungeonInstance::BindParticipantPlayer(
    SessionId sessionId,
    PlayerId playerId)
{
    if (sessionId == 0 || playerId == 0)
    {
        return false;
    }

    std::lock_guard lock(participantMutex_);
    if (!players_.contains(sessionId))
    {
        return false;
    }

    const auto existingPlayerIt = participantPlayerIds_.find(sessionId);
    if (existingPlayerIt != participantPlayerIds_.end())
    {
        return existingPlayerIt->second == playerId;
    }

    const auto existingSessionIt = playerSessions_.find(playerId);
    if (existingSessionIt != playerSessions_.end())
    {
        return existingSessionIt->second == sessionId;
    }

    participantPlayerIds_.emplace(sessionId, playerId);
    playerSessions_.emplace(playerId, sessionId);
    return true;
}

bool DungeonInstance::DisconnectParticipant(
    SessionId sessionId,
    std::chrono::steady_clock::time_point disconnectedAt)
{
    std::lock_guard lock(participantMutex_);
    if (!players_.contains(sessionId))
    {
        return false;
    }

    disconnectedSince_.try_emplace(sessionId, disconnectedAt);
    return true;
}

std::optional<DungeonParticipantReconnect>
DungeonInstance::ReconnectParticipant(
    PlayerId playerId,
    SessionId newSessionId,
    std::chrono::steady_clock::time_point now,
    std::chrono::milliseconds reconnectGrace)
{
    if (playerId == 0 || newSessionId == 0 ||
        reconnectGrace <= std::chrono::milliseconds::zero())
    {
        return std::nullopt;
    }

    std::lock_guard lock(participantMutex_);

    const auto sessionIt = playerSessions_.find(playerId);
    if (sessionIt == playerSessions_.end())
    {
        return std::nullopt;
    }

    const SessionId oldSessionId = sessionIt->second;
    const auto disconnectedIt = disconnectedSince_.find(oldSessionId);
    if (disconnectedIt == disconnectedSince_.end() ||
        players_.contains(newSessionId) ||
        now < disconnectedIt->second ||
        now - disconnectedIt->second >= reconnectGrace)
    {
        return std::nullopt;
    }
    const auto disconnectedAt = disconnectedIt->second;

    auto playerNode = players_.extract(oldSessionId);
    if (playerNode.empty() ||
        !playerNode.mapped()->RebindSession(newSessionId))
    {
        return std::nullopt;
    }

    playerNode.key() = newSessionId;
    players_.insert(std::move(playerNode));

    participantPlayerIds_.erase(oldSessionId);
    participantPlayerIds_.emplace(newSessionId, playerId);
    sessionIt->second = newSessionId;
    disconnectedSince_.erase(oldSessionId);

    const auto participantIt = std::find(
        participants_.begin(),
        participants_.end(),
        oldSessionId);
    if (participantIt != participants_.end())
    {
        *participantIt = newSessionId;
    }

    return DungeonParticipantReconnect{
        oldSessionId,
        disconnectedAt};
}

bool DungeonInstance::RollbackParticipantReconnect(
    PlayerId playerId,
    SessionId newSessionId,
    const DungeonParticipantReconnect& reconnect)
{
    if (playerId == 0 || newSessionId == 0 ||
        reconnect.replacedSessionId == 0)
    {
        return false;
    }

    std::lock_guard lock(participantMutex_);
    const auto sessionIt = playerSessions_.find(playerId);
    if (sessionIt == playerSessions_.end() ||
        sessionIt->second != newSessionId ||
        !players_.contains(newSessionId) ||
        players_.contains(reconnect.replacedSessionId))
    {
        return false;
    }

    auto playerNode = players_.extract(newSessionId);
    if (playerNode.empty() ||
        !playerNode.mapped()->RebindSession(
            reconnect.replacedSessionId))
    {
        return false;
    }

    playerNode.key() = reconnect.replacedSessionId;
    players_.insert(std::move(playerNode));

    participantPlayerIds_.erase(newSessionId);
    participantPlayerIds_.emplace(
        reconnect.replacedSessionId,
        playerId);
    sessionIt->second = reconnect.replacedSessionId;
    disconnectedSince_.emplace(
        reconnect.replacedSessionId,
        reconnect.disconnectedAt);

    const auto participantIt = std::find(
        participants_.begin(),
        participants_.end(),
        newSessionId);
    if (participantIt != participants_.end())
    {
        *participantIt = reconnect.replacedSessionId;
    }

    return true;
}

std::vector<SessionId>
DungeonInstance::RemoveExpiredDisconnectedParticipants(
    std::chrono::steady_clock::time_point now,
    std::chrono::milliseconds reconnectGrace)
{
    std::vector<SessionId> removedSessions;
    if (reconnectGrace <= std::chrono::milliseconds::zero())
    {
        return removedSessions;
    }

    std::lock_guard lock(participantMutex_);

    for (auto disconnectedIt = disconnectedSince_.begin();
         disconnectedIt != disconnectedSince_.end();)
    {
        if (now < disconnectedIt->second ||
            now - disconnectedIt->second < reconnectGrace)
        {
            ++disconnectedIt;
            continue;
        }

        const SessionId sessionId = disconnectedIt->first;
        const auto playerIdIt = participantPlayerIds_.find(sessionId);
        if (playerIdIt != participantPlayerIds_.end())
        {
            playerSessions_.erase(playerIdIt->second);
            participantPlayerIds_.erase(playerIdIt);
        }

        players_.erase(sessionId);
        participants_.erase(
            std::remove(
                participants_.begin(),
                participants_.end(),
                sessionId),
            participants_.end());
        removedSessions.push_back(sessionId);
        disconnectedIt = disconnectedSince_.erase(disconnectedIt);
    }

    return removedSessions;
}

std::vector<SessionId> DungeonInstance::RemoveAllParticipants()
{
    std::lock_guard lock(participantMutex_);
    std::vector<SessionId> removedSessions = participants_;
    participants_.clear();
    players_.clear();
    participantPlayerIds_.clear();
    playerSessions_.clear();
    disconnectedSince_.clear();
    return removedSessions;
}

bool DungeonInstance::IsParticipantDisconnected(SessionId sessionId) const
{
    std::lock_guard lock(participantMutex_);
    return disconnectedSince_.contains(sessionId);
}

bool DungeonInstance::HasDisconnectedParticipants() const
{
    std::lock_guard lock(participantMutex_);
    return !disconnectedSince_.empty();
}

std::size_t DungeonInstance::ConnectedParticipantCount() const
{
    std::lock_guard lock(participantMutex_);
    return players_.size() - disconnectedSince_.size();
}

std::chrono::steady_clock::time_point DungeonInstance::CreatedAt() const
{
    return createdAt_;
}

std::shared_ptr<RoomState> DungeonInstance::FindRoom(RoomId roomId) const
{
    auto roomIt = rooms_.find(roomId);
    if (roomIt == rooms_.end())
    {
        return nullptr;
    }

    return roomIt->second;
}

std::shared_ptr<DungeonPlayerState> DungeonInstance::FindPlayer(
    SessionId sessionId) const
{
    std::lock_guard lock(participantMutex_);
    auto playerIt = players_.find(sessionId);
    if (playerIt == players_.end())
    {
        return nullptr;
    }

    return playerIt->second;
}

std::vector<DungeonEnemySnapshot> DungeonInstance::EnemySnapshots() const
{
    std::vector<DungeonEnemySnapshot> snapshots;

    for (const RoomTemplate& roomTemplate : dungeonTemplate_.rooms)
    {
        const auto room = FindRoom(roomTemplate.id);
        if (room == nullptr)
        {
            continue;
        }

        for (const EnemyState& enemy : room->Enemies())
        {
            snapshots.push_back({roomTemplate.id, enemy});
        }
    }

    return snapshots;
}

std::size_t DungeonInstance::AdvanceRoomWaves()
{
    if (State() != DungeonState::Running)
    {
        return 0;
    }

    std::vector<std::shared_ptr<DungeonPlayerState>> players;
    {
        std::lock_guard lock(participantMutex_);
        players.reserve(players_.size());
        for (const auto& entry : players_)
        {
            if (!disconnectedSince_.contains(entry.first))
            {
                players.push_back(entry.second);
            }
        }
    }

    std::unordered_set<RoomId> occupiedRoomIds;
    for (const auto& player : players)
    {
        occupiedRoomIds.insert(player->CurrentRoom());
    }

    std::size_t startedWaveCount = 0;
    for (RoomId roomId : occupiedRoomIds)
    {
        const auto room = FindRoom(roomId);
        if (room != nullptr && room->StartNextWave())
        {
            ++startedWaveCount;
        }
    }

    return startedWaveCount;
}

UsePortalResult DungeonInstance::TryUsePortal(SessionId sessionId)
{
    if (State() != DungeonState::Running)
    {
        return UsePortalResult::DungeonNotRunning;
    }

    const auto player = FindPlayer(sessionId);
    if (player == nullptr)
    {
        return UsePortalResult::PlayerNotFound;
    }

    const DungeonPlayerSnapshot playerSnapshot = player->Snapshot();
    const auto room = FindRoom(playerSnapshot.roomId);
    if (room == nullptr)
    {
        return UsePortalResult::TargetRoomNotFound;
    }

    const auto roomTemplateIt = std::find_if(
        dungeonTemplate_.rooms.begin(),
        dungeonTemplate_.rooms.end(),
        [roomId = playerSnapshot.roomId](const RoomTemplate& roomTemplate)
        {
            return roomTemplate.id == roomId;
        });

    if (roomTemplateIt == dungeonTemplate_.rooms.end())
    {
        return UsePortalResult::TargetRoomNotFound;
    }

    const auto portalIt = std::find_if(
        roomTemplateIt->portals.begin(),
        roomTemplateIt->portals.end(),
        [&playerSnapshot](const PortalTemplate& portal)
        {
            return IsInsideCollisionBox(
                portal.triggerArea,
                playerSnapshot.position);
        });

    if (portalIt == roomTemplateIt->portals.end())
    {
        return UsePortalResult::NotInsidePortal;
    }

    if (portalIt->requiresRoomClear && !room->IsCleared())
    {
        return UsePortalResult::RoomNotCleared;
    }

    const auto targetRoom = FindRoom(portalIt->targetRoomId);
    if (targetRoom == nullptr)
    {
        return UsePortalResult::TargetRoomNotFound;
    }

    if (player->EnterRoom(*targetRoom, portalIt->targetPosition) !=
        MovePlayerResult::Success)
    {
        return UsePortalResult::TargetPositionBlocked;
    }

    targetRoom->StartNextWave();
    return UsePortalResult::Success;
}

bool DungeonInstance::TryFinishIfCleared()
{
    if (State() != DungeonState::Running)
    {
        return false;
    }

    const RoomId finalRoomId = dungeonTemplate_.rooms.back().id;

    std::vector<std::shared_ptr<DungeonPlayerState>> players;
    {
        std::lock_guard lock(participantMutex_);
        players.reserve(players_.size());
        for (const auto& entry : players_)
        {
            players.push_back(entry.second);
        }
    }

    if (players.empty())
    {
        return false;
    }

    for (const auto& player : players)
    {
        if (player->CurrentRoom() != finalRoomId)
        {
            return false;
        }
    }

    for (const auto& [roomId, room] : rooms_)
    {
        (void)roomId;
        if (!room->IsCleared())
        {
            return false;
        }
    }

    return Finish();
}

bool DungeonInstance::Start()
{
    std::lock_guard lock(stateMutex_);

    if (state_ != DungeonState::Waiting)
    {
        return false;
    }

    state_ = DungeonState::Running;

    const RoomId firstRoomId = dungeonTemplate_.rooms.front().id;
    rooms_.at(firstRoomId)->StartNextWave();
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
