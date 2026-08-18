#include "RoomState.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace dnf
{
namespace
{
DungeonEntityId MakeEnemyEntityId(RoomId roomId, EnemySpawnId spawnId)
{
    return (static_cast<DungeonEntityId>(roomId) << 32) | spawnId;
}
} // namespace

RoomState::RoomState(
    RoomTemplate roomTemplate,
    const EnemyCatalog& enemyCatalog)
    : roomTemplate_(std::move(roomTemplate)),
      enemyCatalog_(enemyCatalog)
{
    for (const EnemySpawnTemplate& spawn : roomTemplate_.enemySpawns)
    {
        lastWave_ = std::max(lastWave_, spawn.wave);
    }

    for (const ObstacleTemplate& obstacle : roomTemplate_.obstacles)
    {
        ObstacleState state;
        state.obstacleId = obstacle.id;
        state.currentHp = obstacle.maxHp;
        state.destructible = obstacle.destructible;
        obstacles_.push_back(state);
    }
}

RoomId RoomState::Id() const
{
    return roomTemplate_.id;
}

std::uint32_t RoomState::CurrentWave() const
{
    std::lock_guard lock(mutex_);
    return currentWave_;
}

bool RoomState::StartNextWave()
{
    std::lock_guard lock(mutex_);

    if (HasLivingEnemy())
    {
        return false;
    }

    std::uint32_t nextWave = 0;

    for (const EnemySpawnTemplate& spawn : roomTemplate_.enemySpawns)
    {
        if (spawn.wave > currentWave_ &&
            (nextWave == 0 || spawn.wave < nextWave))
        {
            nextWave = spawn.wave;
        }
    }

    if (nextWave == 0)
    {
        return false;
    }

    for (const EnemySpawnTemplate& spawn : roomTemplate_.enemySpawns)
    {
        if (spawn.wave != nextWave)
        {
            continue;
        }

        const auto enemyTemplate =
            enemyCatalog_.GetEnemy(spawn.enemyTemplateId);
        if (!enemyTemplate.has_value())
        {
            throw std::runtime_error("Enemy template not found");
        }

        EnemyState enemy;
        enemy.entityId = MakeEnemyEntityId(roomTemplate_.id, spawn.id);
        enemy.spawnId = spawn.id;
        enemy.enemyTemplateId = spawn.enemyTemplateId;
        enemy.position = spawn.position;
        enemy.currentHp = enemyTemplate->maxHp;
        enemies_.push_back(enemy);
    }

    currentWave_ = nextWave;
    return true;
}

bool RoomState::ApplyEnemyDamage(
    DungeonEntityId entityId,
    std::uint32_t damage)
{
    std::lock_guard lock(mutex_);

    auto enemyIt = std::find_if(
        enemies_.begin(),
        enemies_.end(),
        [entityId](const EnemyState& enemy)
        {
            return enemy.entityId == entityId;
        });

    if (enemyIt == enemies_.end() || !enemyIt->alive || damage == 0)
    {
        return false;
    }

    if (damage >= enemyIt->currentHp)
    {
        enemyIt->currentHp = 0;
        enemyIt->alive = false;
    }
    else
    {
        enemyIt->currentHp -= damage;
    }

    return true;
}

bool RoomState::ApplyObstacleDamage(
    ObstacleId obstacleId,
    std::uint32_t damage)
{
    std::lock_guard lock(mutex_);

    auto obstacleIt = std::find_if(
        obstacles_.begin(),
        obstacles_.end(),
        [obstacleId](const ObstacleState& obstacle)
        {
            return obstacle.obstacleId == obstacleId;
        });

    if (obstacleIt == obstacles_.end() ||
        !obstacleIt->destructible ||
        obstacleIt->destroyed ||
        damage == 0)
    {
        return false;
    }

    if (damage >= obstacleIt->currentHp)
    {
        obstacleIt->currentHp = 0;
        obstacleIt->destroyed = true;
    }
    else
    {
        obstacleIt->currentHp -= damage;
    }

    return true;
}

bool RoomState::IsCleared() const
{
    std::lock_guard lock(mutex_);
    return currentWave_ == lastWave_ && !HasLivingEnemy();
}

std::vector<EnemyState> RoomState::Enemies() const
{
    std::lock_guard lock(mutex_);
    return enemies_;
}

std::vector<ObstacleState> RoomState::Obstacles() const
{
    std::lock_guard lock(mutex_);
    return obstacles_;
}

bool RoomState::HasLivingEnemy() const
{
    return std::any_of(
        enemies_.begin(),
        enemies_.end(),
        [](const EnemyState& enemy)
        {
            return enemy.alive;
        });
}
} // namespace dnf
