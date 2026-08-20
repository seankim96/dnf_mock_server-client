#pragma once

#include "DungeonCatalog.h"
#include "DungeonInstance.h"
#include "PartyManager.h"

#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace dnf
{
enum class CreateDungeonStatus
{
    Success,
    PartyNotFound,
    DungeonTemplateNotFound,
    PartyAlreadyInDungeon
};

struct CreateDungeonResult
{
    CreateDungeonStatus status = CreateDungeonStatus::PartyNotFound;
    std::shared_ptr<DungeonInstance> dungeon;
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
    std::shared_ptr<DungeonInstance> FindDungeon(DungeonId dungeonId) const;
    std::shared_ptr<DungeonInstance> FindDungeonByParty(PartyId partyId) const;
    std::vector<DungeonTemplate> GetDungeonTemplates() const;
    std::optional<DungeonTemplate> GetDungeonTemplate(
        DungeonTemplateId templateId) const;
    std::optional<EnemyTemplate> GetEnemyTemplate(
        EnemyTemplateId enemyTemplateId) const;

    bool StartDungeon(DungeonId dungeonId);
    bool CancelDungeon(DungeonId dungeonId);
    bool FinishDungeon(DungeonId dungeonId);
    std::vector<DungeonId> WaitingDungeonIds() const;
    std::vector<DungeonId> RunningDungeonIds() const;
    std::size_t ActiveDungeonCount() const;

private:
    PartyManager& partyManager_;
    DungeonCatalog& dungeonCatalog_;
    const EnemyCatalog& enemyCatalog_;

    mutable std::mutex mutex_;
    DungeonId nextDungeonId_ = 1;
    std::unordered_map<DungeonId, std::shared_ptr<DungeonInstance>> dungeons_;
    std::unordered_map<PartyId, DungeonId> partyDungeons_;
};
} // namespace dnf
