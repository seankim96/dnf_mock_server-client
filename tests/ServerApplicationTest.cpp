#include "ServerApplication.h"

#include <cassert>
#include <iostream>

namespace
{
void TestGameDataIsLoaded()
{
    dnf::ServerApplication application(0);

    const auto iceSlash = application.Skills().GetSkill(1001);
    assert(iceSlash.has_value());
    assert(iceSlash->name == "Ice Slash");
    assert(iceSlash->effects.size() == 2);

    const auto goblin = application.Enemies().GetEnemy(2001);
    assert(goblin.has_value());
    assert(goblin->name == "Goblin");
    assert(goblin->maxHp == 100);

    const auto forest = application.DungeonTemplates().GetDungeon(1001);
    assert(forest.has_value());
    assert(forest->name == "Forest");
    assert(forest->rooms.size() == 2);
    assert(forest->rooms[0].enemySpawns.size() == 1);
    assert(forest->rooms[0].enemySpawns[0].enemyTemplateId == 2001);

    assert(application.DungeonInstances().ActiveDungeonCount() == 0);
    assert(application.DungeonUdpSockets().AllocationCount() == 0);
}
} // namespace

int main()
{
    TestGameDataIsLoaded();

    std::cout << "All server application tests passed.\n";
    return 0;
}
