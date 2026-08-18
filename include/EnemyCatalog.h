#pragma once

#include "DungeonRoom.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace dnf
{
using EnemyTemplateId = std::uint32_t;

enum class EnemyAiType
{
    Melee,
    Ranged,
    Boss
};

struct EnemyTemplate
{
    EnemyTemplateId id = 0;
    std::string name;

    std::uint32_t maxHp = 0;
    float moveSpeed = 0.0f;
    EnemyAiType aiType = EnemyAiType::Melee;

    // 적 위치를 기준으로 한 몸체 충돌 영역
    CollisionBox collision;
};

class EnemyCatalog
{
public:
    bool AddEnemy(EnemyTemplate enemy);
    std::optional<EnemyTemplate> GetEnemy(EnemyTemplateId enemyId) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<EnemyTemplateId, EnemyTemplate> enemies_;
};
} // namespace dnf
