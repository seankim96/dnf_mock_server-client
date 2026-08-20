#include "PlayerRepository.h"

#include <cassert>
#include <iostream>

namespace
{
dnf::PlayerProfile MakeValidProfile()
{
    dnf::PlayerProfile profile;
    profile.playerId = 10;
    profile.name = "Player_1";
    profile.level = 12;
    profile.skillPoints = 3;
    profile.skills = {{1001, 2}, {1002, 1}};
    return profile;
}

void TestValidProfile()
{
    assert(dnf::IsValidPlayerProfile(MakeValidProfile()));
}

void TestInvalidIdentityAndLevel()
{
    dnf::PlayerProfile profile = MakeValidProfile();
    profile.playerId = 0;
    assert(!dnf::IsValidPlayerProfile(profile));

    profile = MakeValidProfile();
    profile.name = "Invalid Name";
    assert(!dnf::IsValidPlayerProfile(profile));

    profile = MakeValidProfile();
    profile.level = 0;
    assert(!dnf::IsValidPlayerProfile(profile));
}

void TestInvalidSkillData()
{
    dnf::PlayerProfile profile = MakeValidProfile();
    profile.skills.push_back({1001, 3});
    assert(!dnf::IsValidPlayerProfile(profile));

    profile = MakeValidProfile();
    profile.skills[0].skillId = 0;
    assert(!dnf::IsValidPlayerProfile(profile));

    profile = MakeValidProfile();
    profile.skills[0].level = 0;
    assert(!dnf::IsValidPlayerProfile(profile));
}
} // namespace

int main()
{
    TestValidProfile();
    TestInvalidIdentityAndLevel();
    TestInvalidSkillData();

    std::cout << "All player profile tests passed.\n";
    return 0;
}
