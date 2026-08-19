#include "SkillCatalog.h"

#include <cassert>
#include <iostream>

namespace
{
dnf::SkillTemplate MakeIceSlash()
{
    dnf::SkillTemplate skill;
    skill.id = 1001;
    skill.name = "Ice Slash";
    skill.cooldownTicks = 90;
    skill.manaCost = 20;
    skill.startupTicks = 5;
    skill.activeTicks = 3;
    skill.recoveryTicks = 10;
    skill.hitBox = {150.0f, 0.0f, 60.0f, 120.0f};
    skill.effects = {
        {dnf::SkillEffectType::Damage, dnf::SkillStat::None, 1.5f, 0},
        {dnf::SkillEffectType::Debuff, dnf::SkillStat::MoveSpeed, 0.3f, 150}};
    return skill;
}

void TestAddAndGetCompositeSkill()
{
    dnf::SkillCatalog catalog;

    assert(catalog.AddSkill(MakeIceSlash()));
    assert(!catalog.AddSkill(MakeIceSlash()));
    assert(catalog.SkillCount() == 1);

    const auto skill = catalog.GetSkill(1001);
    assert(skill.has_value());
    assert(skill->name == "Ice Slash");
    assert(skill->effects.size() == 2);
    assert(skill->effects[0].type == dnf::SkillEffectType::Damage);
    assert(skill->effects[1].type == dnf::SkillEffectType::Debuff);
    assert(skill->effects[1].stat == dnf::SkillStat::MoveSpeed);
}

void TestInvalidSkill()
{
    dnf::SkillCatalog catalog;

    auto skill = MakeIceSlash();
    skill.id = 0;
    assert(!catalog.AddSkill(skill));

    skill = MakeIceSlash();
    skill.activeTicks = 0;
    assert(!catalog.AddSkill(skill));

    skill = MakeIceSlash();
    skill.effects[1].durationTicks = 0;
    assert(!catalog.AddSkill(skill));

    skill = MakeIceSlash();
    skill.hitBox.forwardRange = 0.0f;
    assert(!catalog.AddSkill(skill));

    assert(!catalog.GetSkill(9999).has_value());
}
} // namespace

int main()
{
    TestAddAndGetCompositeSkill();
    TestInvalidSkill();

    std::cout << "All skill catalog tests passed.\n";
    return 0;
}
