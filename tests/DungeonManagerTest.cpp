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

    dnf::DungeonCatalog dungeonCatalog;
    dungeonCatalog.AddDungeon(1001, "Forest", 3);

    dnf::DungeonManager dungeonManager(partyManager, dungeonCatalog);
    const auto result = dungeonManager.CreateDungeon(partyId, 1001);

    assert(result.status == dnf::CreateDungeonStatus::Success);
    assert(result.dungeon != nullptr);
    assert(result.dungeon->TemplateId() == 1001);
    assert(result.dungeon->Party() == partyId);
    assert(result.dungeon->HasParticipant(100));
    assert(result.dungeon->HasParticipant(200));
    assert(dungeonManager.ActiveDungeonCount() == 1);
}

void TestCreationFailure()
{
    dnf::PartyManager partyManager;
    dnf::DungeonCatalog dungeonCatalog;
    dungeonCatalog.AddDungeon(1001, "Forest", 3);
    dnf::DungeonManager dungeonManager(partyManager, dungeonCatalog);

    const auto missingParty = dungeonManager.CreateDungeon(999, 1001);
    assert(missingParty.status == dnf::CreateDungeonStatus::PartyNotFound);

    const dnf::PartyId partyId = partyManager.CreateParty(100).value();
    const auto missingTemplate = dungeonManager.CreateDungeon(partyId, 9999);
    assert(missingTemplate.status ==
           dnf::CreateDungeonStatus::DungeonTemplateNotFound);

    const auto first = dungeonManager.CreateDungeon(partyId, 1001);
    const auto duplicate = dungeonManager.CreateDungeon(partyId, 1001);

    assert(first.status == dnf::CreateDungeonStatus::Success);
    assert(duplicate.status ==
           dnf::CreateDungeonStatus::PartyAlreadyInDungeon);
    assert(duplicate.dungeon == nullptr);
}

void TestDungeonLifecycle()
{
    dnf::PartyManager partyManager;
    const dnf::PartyId partyId = partyManager.CreateParty(100).value();

    dnf::DungeonCatalog dungeonCatalog;
    dungeonCatalog.AddDungeon(1001, "Forest", 3);
    dnf::DungeonManager dungeonManager(partyManager, dungeonCatalog);
    const auto created = dungeonManager.CreateDungeon(partyId, 1001);
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

    const auto nextDungeon = dungeonManager.CreateDungeon(partyId, 1001);
    assert(nextDungeon.status == dnf::CreateDungeonStatus::Success);
    assert(nextDungeon.dungeon->Id() != dungeonId);
}

void TestParticipantSnapshot()
{
    dnf::PartyManager partyManager;
    const dnf::PartyId partyId = partyManager.CreateParty(100).value();
    partyManager.JoinParty(partyId, 200);

    dnf::DungeonCatalog dungeonCatalog;
    dungeonCatalog.AddDungeon(1001, "Forest", 3);
    dnf::DungeonManager dungeonManager(partyManager, dungeonCatalog);
    const auto created = dungeonManager.CreateDungeon(partyId, 1001);

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
