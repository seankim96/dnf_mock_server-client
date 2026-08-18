#include "DungeonCatalog.h"

#include <cassert>
#include <iostream>

namespace
{
void TestAddAndGetDungeon()
{
    dnf::DungeonCatalog catalog;

    assert(catalog.AddDungeon(1001, "Forest", 3));
    assert(!catalog.AddDungeon(1001, "Duplicate", 5));

    const auto dungeon = catalog.GetDungeon(1001);
    assert(dungeon.has_value());
    assert(dungeon->id == 1001);
    assert(dungeon->name == "Forest");
    assert(dungeon->roomCount == 3);
}

void TestInvalidDungeon()
{
    dnf::DungeonCatalog catalog;

    assert(!catalog.AddDungeon(0, "Forest", 3));
    assert(!catalog.AddDungeon(1001, "", 3));
    assert(!catalog.AddDungeon(1001, "Forest", 0));
    assert(!catalog.GetDungeon(9999).has_value());
}
} // namespace

int main()
{
    TestAddAndGetDungeon();
    TestInvalidDungeon();

    std::cout << "All dungeon catalog tests passed.\n";
    return 0;
}
