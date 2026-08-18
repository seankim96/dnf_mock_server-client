#include "DungeonRoom.h"

#include <cassert>
#include <iostream>

namespace
{
void TestRoomBoundary()
{
    dnf::RoomTemplate room;
    room.id = 1;
    room.width = 1200.0f;
    room.depth = 500.0f;

    assert(dnf::IsInsideRoom(room, {100.0f, 250.0f, 0.0f}));
    assert(dnf::IsInsideRoom(room, {1200.0f, 500.0f, 100.0f}));
    assert(!dnf::IsInsideRoom(room, {-1.0f, 250.0f, 0.0f}));
    assert(!dnf::IsInsideRoom(room, {100.0f, 501.0f, 0.0f}));
}

void TestCollisionBox()
{
    const dnf::CollisionBox box{
        {100.0f, 200.0f, 0.0f},
        {200.0f, 300.0f, 150.0f}};

    assert(dnf::IsValidCollisionBox(box));
    assert(dnf::IsInsideCollisionBox(box, {150.0f, 250.0f, 50.0f}));
    assert(dnf::IsInsideCollisionBox(box, {100.0f, 200.0f, 0.0f}));
    assert(!dnf::IsInsideCollisionBox(box, {201.0f, 250.0f, 50.0f}));

    const dnf::CollisionBox invalidBox{
        {200.0f, 200.0f, 0.0f},
        {100.0f, 300.0f, 150.0f}};
    assert(!dnf::IsValidCollisionBox(invalidBox));
}
} // namespace

int main()
{
    TestRoomBoundary();
    TestCollisionBox();

    std::cout << "All dungeon room tests passed.\n";
    return 0;
}
