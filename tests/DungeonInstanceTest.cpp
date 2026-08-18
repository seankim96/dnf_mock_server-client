#include "DungeonInstance.h"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void TestDungeonInformation()
{
    const dnf::DungeonInstance dungeon(10, 20, {100, 200});

    assert(dungeon.Id() == 10);
    assert(dungeon.Party() == 20);
    assert(dungeon.Participants() == std::vector<dnf::SessionId>({100, 200}));
    assert(dungeon.HasParticipant(100));
    assert(!dungeon.HasParticipant(999));
}

void TestDungeonState()
{
    dnf::DungeonInstance dungeon(10, 20, {100});

    assert(dungeon.State() == dnf::DungeonState::Waiting);
    assert(!dungeon.Finish());

    assert(dungeon.Start());
    assert(dungeon.State() == dnf::DungeonState::Running);
    assert(!dungeon.Start());

    assert(dungeon.Finish());
    assert(dungeon.State() == dnf::DungeonState::Finished);
    assert(!dungeon.Finish());
}

void TestInvalidParticipantCount()
{
    bool errorOccurred = false;

    try
    {
        dnf::DungeonInstance dungeon(10, 20, {});
    }
    catch (const std::invalid_argument&)
    {
        errorOccurred = true;
    }

    assert(errorOccurred);
}
} // namespace

int main()
{
    TestDungeonInformation();
    TestDungeonState();
    TestInvalidParticipantCount();

    std::cout << "All dungeon instance tests passed.\n";
    return 0;
}
