#pragma once

#include "DungeonRoom.h"
#include "EnemyCatalog.h"

#include <cstdint>
#include <mutex>
#include <vector>

namespace dnf
{
using DungeonEntityId = std::uint64_t;

struct EnemyState
{
    DungeonEntityId entityId = 0;
    EnemySpawnId spawnId = 0;
    EnemyTemplateId enemyTemplateId = 0;

    Position position;
    std::uint32_t currentHp = 0;
    bool alive = true;
};

struct ObstacleState
{
    ObstacleId obstacleId = 0;
    std::uint32_t currentHp = 0;
    bool destructible = false;
    bool destroyed = false;
};

enum class PositionCheckResult
{
    Valid,
    OutsideRoom,
    BlockedByObstacle
};

class RoomState
{
public:
    RoomState(
        RoomTemplate roomTemplate,
        const EnemyCatalog& enemyCatalog);

    RoomId Id() const;
    std::uint32_t CurrentWave() const;

    bool StartNextWave();
    bool ApplyEnemyDamage(DungeonEntityId entityId, std::uint32_t damage);
    bool ApplyObstacleDamage(ObstacleId obstacleId, std::uint32_t damage);
    bool IsCleared() const;
    PositionCheckResult CheckPosition(const Position& position) const;

    std::vector<EnemyState> Enemies() const;
    std::vector<ObstacleState> Obstacles() const;

private:
    bool HasLivingEnemy() const;

    RoomTemplate roomTemplate_;
    const EnemyCatalog& enemyCatalog_;
    std::uint32_t currentWave_ = 0;
    std::uint32_t lastWave_ = 0;

    mutable std::mutex mutex_;
    std::vector<EnemyState> enemies_;
    std::vector<ObstacleState> obstacles_;
};
} // namespace dnf
