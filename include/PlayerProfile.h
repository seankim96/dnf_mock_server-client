#pragma once

#include "PlayerId.h"
#include "PlayerSkillBook.h"

#include <cstdint>
#include <string>
#include <vector>

namespace dnf
{
struct PlayerProfile
{
    PlayerId playerId = 0;
    std::string name;
    std::uint32_t level = 1;
    std::uint32_t skillPoints = 0;
    std::vector<OwnedSkill> skills;
};

bool IsValidPlayerProfile(const PlayerProfile& profile);
} // namespace dnf
