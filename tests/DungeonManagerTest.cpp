#include "DungeonManager.h"

#include <cassert>
#include <iostream>

namespace
{
void TestCreateDungeonFromParty()
{
    dnf::PartyManager partyManager;
    const dnf::PartyId partyId = partyManager.CreateParty(100).value();
    partyManager.JoinParty(partyId, 200);

    dnf::DungeonManager dungeonManager(partyManager);
    const auto result = dungeonManager.CreateDungeon(partyId);

    assert(result.status == dnf::CreateDungeonStatus::Success);
    assert(result.dungeon != nullptr);
    assert(result.dungeon->Party() == partyId);
    assert(result.dungeon->HasParticipant(100));
    assert(result.dungeon->HasParticipant(200));
    assert(dungeonManager.ActiveDungeonCount() == 1);
}

void TestCreationFailure()
{
    dnf::PartyManager partyManager;
    dnf::DungeonManager dungeonManager(partyManager);

    const auto missingParty = dungeonManager.CreateDungeon(999);
    assert(missingParty.status == dnf::CreateDungeonStatus::PartyNotFound);

    const dnf::PartyId partyId = partyManager.CreateParty(100).value();
    const auto first = dungeonManager.CreateDungeon(partyId);
    const auto duplicate = dungeonManager.CreateDungeon(partyId);

    assert(first.status == dnf::CreateDungeonStatus::Success);
    assert(duplicate.status ==
           dnf::CreateDungeonStatus::PartyAlreadyInDungeon);
    assert(duplicate.dungeon == nullptr);
}

void TestDungeonLifecycle()
{
    dnf::PartyManager partyManager;
    const dnf::PartyId partyId = partyManager.CreateParty(100).value();

    dnf::DungeonManager dungeonManager(partyManager);
    const auto created = dungeonManager.CreateDungeon(partyId);
    const dnf::DungeonId dungeonId = created.dungeon->Id();

    assert(dungeonManager.FindDungeon(dungeonId) == created.dungeon);
    assert(dungeonManager.FindDungeonByParty(partyId) == created.dungeon);
    assert(!dungeonManager.FinishDungeon(dungeonId));

    assert(dungeonManager.StartDungeon(dungeonId));
    assert(!dungeonManager.StartDungeon(dungeonId));
    assert(dungeonManager.FinishDungeon(dungeonId));

    assert(dungeonManager.FindDungeon(dungeonId) == nullptr);
    assert(dungeonManager.FindDungeonByParty(partyId) == nullptr);
    assert(dungeonManager.ActiveDungeonCount() == 0);

    const auto nextDungeon = dungeonManager.CreateDungeon(partyId);
    assert(nextDungeon.status == dnf::CreateDungeonStatus::Success);
    assert(nextDungeon.dungeon->Id() != dungeonId);
}

void TestParticipantSnapshot()
{
    dnf::PartyManager partyManager;
    const dnf::PartyId partyId = partyManager.CreateParty(100).value();
    partyManager.JoinParty(partyId, 200);

    dnf::DungeonManager dungeonManager(partyManager);
    const auto created = dungeonManager.CreateDungeon(partyId);

    partyManager.LeaveParty(200);

    assert(created.dungeon->HasParticipant(200));
    assert(created.dungeon->Participants().size() == 2);
}
} // namespace

int main()
{
    TestCreateDungeonFromParty();
    TestCreationFailure();
    TestDungeonLifecycle();
    TestParticipantSnapshot();

    std::cout << "All dungeon manager tests passed.\n";
    return 0;
}
