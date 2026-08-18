#include "EnemyCatalog.h"

#include <cassert>
#include <iostream>

namespace
{
dnf::EnemyTemplate MakeGoblin()
{
    dnf::EnemyTemplate enemy;
    enemy.id = 2001;
    enemy.name = "Goblin";
    enemy.maxHp = 100;
    enemy.moveSpeed = 120.0f;
    enemy.aiType = dnf::EnemyAiType::Melee;
    enemy.collision = {
        {-20.0f, -15.0f, 0.0f},
        {20.0f, 15.0f, 80.0f}};
    return enemy;
}

void TestAddAndGetEnemy()
{
    dnf::EnemyCatalog catalog;

    assert(catalog.AddEnemy(MakeGoblin()));
    assert(!catalog.AddEnemy(MakeGoblin()));

    const auto enemy = catalog.GetEnemy(2001);
    assert(enemy.has_value());
    assert(enemy->name == "Goblin");
    assert(enemy->maxHp == 100);
    assert(enemy->moveSpeed == 120.0f);
    assert(enemy->aiType == dnf::EnemyAiType::Melee);
}

void TestInvalidEnemy()
{
    dnf::EnemyCatalog catalog;

    auto enemy = MakeGoblin();
    enemy.id = 0;
    assert(!catalog.AddEnemy(enemy));

    enemy = MakeGoblin();
    enemy.maxHp = 0;
    assert(!catalog.AddEnemy(enemy));

    enemy = MakeGoblin();
    enemy.moveSpeed = -1.0f;
    assert(!catalog.AddEnemy(enemy));

    enemy = MakeGoblin();
    enemy.collision.maximum.x = enemy.collision.minimum.x;
    assert(!catalog.AddEnemy(enemy));

    assert(!catalog.GetEnemy(9999).has_value());
}
} // namespace

int main()
{
    TestAddAndGetEnemy();
    TestInvalidEnemy();

    std::cout << "All enemy catalog tests passed.\n";
    return 0;
}
