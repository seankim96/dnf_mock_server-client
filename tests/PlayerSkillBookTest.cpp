#include "PlayerSkillBook.h"

#include <cassert>
#include <iostream>

namespace
{
dnf::SkillTemplate MakeSkill(dnf::SkillId skillId, const char* name)
{
    dnf::SkillTemplate skill;
    skill.id = skillId;
    skill.name = name;
    skill.activeTicks = 1;
    skill.hitBox = {100.0f, 0.0f, 50.0f, 100.0f};
    skill.effects = {
        {dnf::SkillEffectType::Damage,
         dnf::SkillTargetType::Enemy,
         dnf::SkillStat::None,
         1.0f,
         0}};
    return skill;
}

void TestLearnRegisteredSkill()
{
    dnf::SkillCatalog catalog;
    assert(catalog.AddSkill(MakeSkill(1002, "Wave")));
    assert(catalog.AddSkill(MakeSkill(1001, "Slash")));

    dnf::PlayerSkillBook skillBook(catalog);

    assert(skillBook.LearnSkill(1002));
    assert(skillBook.LearnSkill(1001));
    assert(!skillBook.LearnSkill(1001));
    assert(!skillBook.LearnSkill(9999));

    assert(skillBook.HasSkill(1001));
    assert(!skillBook.HasSkill(9999));
    assert(skillBook.SkillLevel(1001) == 1);
    assert(skillBook.SkillLevel(9999) == 0);

    const auto snapshot = skillBook.Snapshot();
    assert(snapshot.size() == 2);
    assert(snapshot[0].skillId == 1001);
    assert(snapshot[0].level == 1);
    assert(snapshot[1].skillId == 1002);
    assert(snapshot[1].level == 1);
}
} // namespace

int main()
{
    TestLearnRegisteredSkill();

    std::cout << "All player skill book tests passed.\n";
    return 0;
}
