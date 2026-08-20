#include "PlayerSkillBook.h"

#include <algorithm>

namespace dnf
{
PlayerSkillBook::PlayerSkillBook(const SkillCatalog& skillCatalog)
    : skillCatalog_(skillCatalog)
{
}

bool PlayerSkillBook::LearnSkill(SkillId skillId)
{
    if (!skillCatalog_.GetSkill(skillId).has_value())
    {
        return false;
    }

    std::lock_guard lock(mutex_);
    return skillLevels_.emplace(skillId, 1).second;
}

bool PlayerSkillBook::HasSkill(SkillId skillId) const
{
    std::lock_guard lock(mutex_);
    return skillLevels_.contains(skillId);
}

std::uint32_t PlayerSkillBook::SkillLevel(SkillId skillId) const
{
    std::lock_guard lock(mutex_);

    const auto skillIt = skillLevels_.find(skillId);
    if (skillIt == skillLevels_.end())
    {
        return 0;
    }

    return skillIt->second;
}

std::vector<OwnedSkill> PlayerSkillBook::Snapshot() const
{
    std::lock_guard lock(mutex_);

    std::vector<OwnedSkill> skills;
    skills.reserve(skillLevels_.size());

    for (const auto& [skillId, level] : skillLevels_)
    {
        skills.push_back({skillId, level});
    }

    std::sort(
        skills.begin(),
        skills.end(),
        [](const OwnedSkill& left, const OwnedSkill& right)
        {
            return left.skillId < right.skillId;
        });

    return skills;
}
} // namespace dnf
