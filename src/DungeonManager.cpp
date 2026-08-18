#include "DungeonManager.h"

namespace dnf
{
DungeonManager::DungeonManager(PartyManager& partyManager)
    : partyManager_(partyManager)
{
}

CreateDungeonResult DungeonManager::CreateDungeon(PartyId partyId)
{
    const auto party = partyManager_.GetParty(partyId);
    if (!party.has_value())
    {
        return {CreateDungeonStatus::PartyNotFound, nullptr};
    }

    std::lock_guard lock(mutex_);

    if (partyDungeons_.contains(partyId))
    {
        return {CreateDungeonStatus::PartyAlreadyInDungeon, nullptr};
    }

    const DungeonId dungeonId = nextDungeonId_++;
    auto dungeon = std::make_shared<DungeonInstance>(
        dungeonId,
        partyId,
        party->members);

    dungeons_.emplace(dungeonId, dungeon);
    partyDungeons_.emplace(partyId, dungeonId);

    return {CreateDungeonStatus::Success, dungeon};
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

bool DungeonManager::FinishDungeon(DungeonId dungeonId)
{
    std::lock_guard lock(mutex_);

    auto dungeonIt = dungeons_.find(dungeonId);
    if (dungeonIt == dungeons_.end() || !dungeonIt->second->Finish())
    {
        return false;
    }

    partyDungeons_.erase(dungeonIt->second->Party());
    dungeons_.erase(dungeonIt);
    return true;
}

std::size_t DungeonManager::ActiveDungeonCount() const
{
    std::lock_guard lock(mutex_);
    return dungeons_.size();
}
} // namespace dnf
