#include "DungeonManager.h"
#include "EnemyCatalog.h"

#include <cassert>
#include <iostream>

namespace
{
void AddTestDungeon(dnf::DungeonCatalog& catalog)
{
    const dnf::RoomTemplate room{
        1, 1200.0f, 500.0f, {100.0f, 250.0f, 0.0f}};
    catalog.AddDungeon(1001, "Forest", {room});
}

void TestCreateDungeonFromParty()
{
    dnf::PartyManager partyManager;
    const dnf::PartyId partyId = partyManager.CreateParty(100).value();
    partyManager.JoinParty(partyId, 200);

    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);
    AddTestDungeon(dungeonCatalog);

    dnf::DungeonManager dungeonManager(
        partyManager, dungeonCatalog, enemyCatalog);
    const auto result = dungeonManager.CreateDungeon(partyId, 1001);

    assert(result.status == dnf::CreateDungeonStatus::Success);
    assert(result.dungeon != nullptr);
    assert(result.dungeon->TemplateId() == 1001);
    assert(result.dungeon->Party() == partyId);
    assert(result.dungeon->HasParticipant(100));
    assert(result.dungeon->HasParticipant(200));
    assert(result.dungeon->FindRoom(1) != nullptr);
    assert(result.dungeon->FindPlayer(100) != nullptr);
    assert(dungeonManager.ActiveDungeonCount() == 1);
}

void TestCreationFailure()
{
    dnf::PartyManager partyManager;
    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);
    AddTestDungeon(dungeonCatalog);
    dnf::DungeonManager dungeonManager(
        partyManager, dungeonCatalog, enemyCatalog);

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

void TestDungeonTemplateList()
{
    dnf::PartyManager partyManager;
    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);
    AddTestDungeon(dungeonCatalog);
    dnf::DungeonManager dungeonManager(
        partyManager, dungeonCatalog, enemyCatalog);

    const auto templates = dungeonManager.GetDungeonTemplates();
    assert(templates.size() == 1);
    assert(templates[0].id == 1001);
    assert(templates[0].name == "Forest");
}

void TestDungeonLifecycle()
{
    dnf::PartyManager partyManager;
    const dnf::PartyId partyId = partyManager.CreateParty(100).value();

    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);
    AddTestDungeon(dungeonCatalog);
    dnf::DungeonManager dungeonManager(
        partyManager, dungeonCatalog, enemyCatalog);
    const auto created = dungeonManager.CreateDungeon(partyId, 1001);
    const dnf::DungeonId dungeonId = created.dungeon->Id();

    assert(dungeonManager.FindDungeon(dungeonId) == created.dungeon);
    assert(dungeonManager.FindDungeonByParty(partyId) == created.dungeon);
    assert(dungeonManager.WaitingDungeonIds() ==
           std::vector<dnf::DungeonId>({dungeonId}));
    assert(dungeonManager.RunningDungeonIds().empty());
    assert(!dungeonManager.FinishDungeon(dungeonId));

    assert(dungeonManager.StartDungeon(dungeonId));
    assert(dungeonManager.WaitingDungeonIds().empty());
    assert(dungeonManager.RunningDungeonIds() ==
           std::vector<dnf::DungeonId>({dungeonId}));
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

    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);
    AddTestDungeon(dungeonCatalog);
    dnf::DungeonManager dungeonManager(
        partyManager, dungeonCatalog, enemyCatalog);
    const auto created = dungeonManager.CreateDungeon(partyId, 1001);

    partyManager.LeaveParty(200);

    assert(created.dungeon->HasParticipant(200));
    assert(created.dungeon->Participants().size() == 2);
}

void TestCancelWaitingDungeon()
{
    dnf::PartyManager partyManager;
    const dnf::PartyId partyId = partyManager.CreateParty(100).value();

    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);
    AddTestDungeon(dungeonCatalog);
    dnf::DungeonManager dungeonManager(
        partyManager, dungeonCatalog, enemyCatalog);

    const auto created = dungeonManager.CreateDungeon(partyId, 1001);
    assert(dungeonManager.CancelDungeon(created.dungeon->Id()));
    assert(dungeonManager.ActiveDungeonCount() == 0);
    assert(!dungeonManager.CancelDungeon(created.dungeon->Id()));
}
} // namespace

int main()
{
    TestCreateDungeonFromParty();
    TestCreationFailure();
    TestDungeonTemplateList();
    TestDungeonLifecycle();
    TestParticipantSnapshot();
    TestCancelWaitingDungeon();

    std::cout << "All dungeon manager tests passed.\n";
    return 0;
}
