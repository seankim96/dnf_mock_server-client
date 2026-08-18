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
} // namespace

int main()
{
    TestRoomBoundary();

    std::cout << "All dungeon room tests passed.\n";
    return 0;
}
