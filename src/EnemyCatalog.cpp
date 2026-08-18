#include "EnemyCatalog.h"

#include <utility>

namespace dnf
{
bool EnemyCatalog::AddEnemy(EnemyTemplate enemy)
{
    std::lock_guard lock(mutex_);

    if (enemy.id == 0 || enemy.name.empty() || enemy.maxHp == 0 ||
        enemy.moveSpeed < 0.0f || !IsValidCollisionBox(enemy.collision) ||
        enemy.collision.minimum.z < 0.0f || enemies_.contains(enemy.id))
    {
        return false;
    }

    enemies_.emplace(enemy.id, std::move(enemy));
    return true;
}

std::optional<EnemyTemplate> EnemyCatalog::GetEnemy(
    EnemyTemplateId enemyId) const
{
    std::lock_guard lock(mutex_);

    auto enemyIt = enemies_.find(enemyId);
    if (enemyIt == enemies_.end())
    {
        return std::nullopt;
    }

    return enemyIt->second;
}
} // namespace dnf
