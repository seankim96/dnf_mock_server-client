#include "SkillCatalog.h"

#include <cmath>
#include <utility>

namespace dnf
{
namespace
{
bool IsValidTarget(const SkillEffect& effect)
{
    switch (effect.type)
    {
    case SkillEffectType::Damage:
    case SkillEffectType::Debuff:
        return effect.target == SkillTargetType::Enemy;

    case SkillEffectType::Heal:
    case SkillEffectType::Buff:
        return effect.target == SkillTargetType::Self ||
               effect.target == SkillTargetType::Party;
    }

    return false;
}

bool IsValidEffect(const SkillEffect& effect)
{
    if (!IsValidTarget(effect) || !std::isfinite(effect.value) ||
        effect.value <= 0.0f)
    {
        return false;
    }

    const bool isStatusEffect =
        effect.type == SkillEffectType::Buff ||
        effect.type == SkillEffectType::Debuff;

    if (isStatusEffect)
    {
        return effect.stat != SkillStat::None && effect.durationTicks > 0;
    }

    return effect.stat == SkillStat::None && effect.durationTicks == 0;
}

bool IsValidHitBox(const SkillHitBox& hitBox)
{
    return std::isfinite(hitBox.forwardRange) &&
           std::isfinite(hitBox.backwardRange) &&
           std::isfinite(hitBox.halfDepth) &&
           std::isfinite(hitBox.height) &&
           hitBox.forwardRange > 0.0f &&
           hitBox.backwardRange >= 0.0f &&
           hitBox.halfDepth > 0.0f &&
           hitBox.height > 0.0f;
}
} // namespace

bool SkillCatalog::AddSkill(SkillTemplate skill)
{
    std::lock_guard lock(mutex_);

    if (skill.id == 0 || skill.name.empty() ||
        skill.activeTicks == 0 || !IsValidHitBox(skill.hitBox) ||
        skill.effects.empty() || skills_.contains(skill.id))
    {
        return false;
    }

    for (const SkillEffect& effect : skill.effects)
    {
        if (!IsValidEffect(effect))
        {
            return false;
        }
    }

    skills_.emplace(skill.id, std::move(skill));
    return true;
}

std::optional<SkillTemplate> SkillCatalog::GetSkill(SkillId skillId) const
{
    std::lock_guard lock(mutex_);

    const auto skillIt = skills_.find(skillId);
    if (skillIt == skills_.end())
    {
        return std::nullopt;
    }

    return skillIt->second;
}

std::size_t SkillCatalog::SkillCount() const
{
    std::lock_guard lock(mutex_);
    return skills_.size();
}
} // namespace dnf
