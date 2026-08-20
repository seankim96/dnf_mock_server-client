#include "PlayerProfile.h"

#include "LoginValidator.h"

#include <unordered_set>

namespace dnf
{
bool IsValidPlayerProfile(const PlayerProfile& profile)
{
    const LoginValidationResult nameValidation =
        LoginValidator().Validate(profile.name);

    if (profile.playerId == 0 || profile.level == 0 ||
        nameValidation.result != LoginSuccess)
    {
        return false;
    }

    std::unordered_set<SkillId> skillIds;
    for (const OwnedSkill& skill : profile.skills)
    {
        if (skill.skillId == 0 || skill.level == 0 ||
            !skillIds.insert(skill.skillId).second)
        {
            return false;
        }
    }

    return true;
}
} // namespace dnf
