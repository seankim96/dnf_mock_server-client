#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dnf
{
using SkillId = std::uint32_t;

enum class SkillEffectType
{
    Damage,
    Heal,
    Buff,
    Debuff
};

enum class SkillTargetType
{
    Self,
    Party,
    Enemy
};

enum class SkillStat
{
    None,
    Attack,
    Defense,
    MoveSpeed
};

struct SkillEffect
{
    SkillEffectType type = SkillEffectType::Damage;
    SkillTargetType target = SkillTargetType::Enemy;
    SkillStat stat = SkillStat::None;
    float value = 0.0f;
    std::uint32_t durationTicks = 0;
};

struct SkillHitBox
{
    float forwardRange = 0.0f;
    float backwardRange = 0.0f;
    float halfDepth = 0.0f;
    float height = 0.0f;
};

struct SkillTemplate
{
    SkillId id = 0;
    std::string name;

    std::uint32_t cooldownTicks = 0;
    std::uint32_t manaCost = 0;
    std::uint32_t startupTicks = 0;
    std::uint32_t activeTicks = 0;
    std::uint32_t recoveryTicks = 0;

    SkillHitBox hitBox;
    std::vector<SkillEffect> effects;
};

class SkillCatalog
{
public:
    bool AddSkill(SkillTemplate skill);
    std::optional<SkillTemplate> GetSkill(SkillId skillId) const;
    std::size_t SkillCount() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<SkillId, SkillTemplate> skills_;
};
} // namespace dnf
