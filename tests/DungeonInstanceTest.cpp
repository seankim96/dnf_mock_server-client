#include "DungeonInstance.h"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
dnf::DungeonTemplate MakeDungeonTemplate()
{
    dnf::DungeonTemplate dungeon;
    dungeon.id = 1001;
    dungeon.name = "Forest";
    dungeon.rooms.push_back(
        {1, 1200.0f, 500.0f, {100.0f, 250.0f, 0.0f}});
    dungeon.rooms.push_back(
        {2, 1500.0f, 600.0f, {100.0f, 300.0f, 0.0f}});
    return dungeon;
}

void TestDungeonInformation()
{
    dnf::EnemyCatalog enemyCatalog;
    const dnf::DungeonInstance dungeon(
        10, MakeDungeonTemplate(), 20, {100, 200}, enemyCatalog);

    assert(dungeon.Id() == 10);
    assert(dungeon.TemplateId() == 1001);
    assert(dungeon.Party() == 20);
    assert(dungeon.Participants() == std::vector<dnf::SessionId>({100, 200}));
    assert(dungeon.HasParticipant(100));
    assert(!dungeon.HasParticipant(999));

    assert(dungeon.FindRoom(1) != nullptr);
    assert(dungeon.FindRoom(2) != nullptr);
    assert(dungeon.FindRoom(999) == nullptr);

    const auto player = dungeon.FindPlayer(100);
    assert(player != nullptr);
    assert(player->CurrentRoom() == 1);
    assert(player->CurrentPosition().x == 100.0f);
    assert(dungeon.FindPlayer(999) == nullptr);
}

void TestDungeonState()
{
    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonInstance dungeon(
        10, MakeDungeonTemplate(), 20, {100}, enemyCatalog);

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
    dnf::EnemyCatalog enemyCatalog;
    bool errorOccurred = false;

    try
    {
        dnf::DungeonInstance dungeon(
            10, MakeDungeonTemplate(), 20, {}, enemyCatalog);
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
