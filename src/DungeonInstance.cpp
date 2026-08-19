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
    auto playerIt = players_.find(sessionId);
    if (playerIt == players_.end())
    {
        return nullptr;
    }

    return playerIt->second;
}

std::size_t DungeonInstance::AdvanceRoomWaves()
{
    if (State() != DungeonState::Running)
    {
        return 0;
    }

    std::unordered_set<RoomId> occupiedRoomIds;
    for (const auto& entry : players_)
    {
        occupiedRoomIds.insert(entry.second->CurrentRoom());
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
