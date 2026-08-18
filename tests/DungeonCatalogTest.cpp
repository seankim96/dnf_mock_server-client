#include "DungeonCatalog.h"

#include <cassert>
#include <iostream>

namespace
{
void TestAddAndGetDungeon()
{
    dnf::DungeonCatalog catalog;

    const dnf::RoomTemplate firstRoom{
        1, 1200.0f, 500.0f, {100.0f, 250.0f, 0.0f}};
    const dnf::RoomTemplate secondRoom{
        2, 1500.0f, 600.0f, {100.0f, 300.0f, 0.0f}};

    assert(catalog.AddDungeon(1001, "Forest", {firstRoom, secondRoom}));
    assert(!catalog.AddDungeon(1001, "Duplicate", {firstRoom}));

    const auto dungeon = catalog.GetDungeon(1001);
    assert(dungeon.has_value());
    assert(dungeon->id == 1001);
    assert(dungeon->name == "Forest");
    assert(dungeon->rooms.size() == 2);
    assert(dungeon->rooms[0].id == 1);
    assert(dungeon->rooms[1].playerSpawn.y == 300.0f);
}

void TestInvalidDungeon()
{
    dnf::DungeonCatalog catalog;

    const dnf::RoomTemplate validRoom{
        1, 1200.0f, 500.0f, {100.0f, 250.0f, 0.0f}};
    const dnf::RoomTemplate invalidSpawn{
        2, 1200.0f, 500.0f, {1300.0f, 250.0f, 0.0f}};

    assert(!catalog.AddDungeon(0, "Forest", {validRoom}));
    assert(!catalog.AddDungeon(1001, "", {validRoom}));
    assert(!catalog.AddDungeon(1001, "Forest", {}));
    assert(!catalog.AddDungeon(1001, "Forest", {invalidSpawn}));
    assert(!catalog.AddDungeon(1001, "Forest", {validRoom, validRoom}));
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
