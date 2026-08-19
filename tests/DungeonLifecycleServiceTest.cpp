#include "DungeonLifecycleService.h"

#include <boost/asio/io_context.hpp>

#include <cassert>
#include <iostream>

namespace
{
void AddTestDungeon(dnf::DungeonCatalog& dungeonCatalog)
{
    const dnf::RoomTemplate room{
        1, 1200.0f, 500.0f, {100.0f, 250.0f, 0.0f}};
    assert(dungeonCatalog.AddDungeon(1001, "Forest", {room}));
}

void TestCancelWaitingDungeonReleasesUdp()
{
    boost::asio::io_context ioContext;
    dnf::DungeonUdpManager udpManager(ioContext);
    dnf::PartyManager partyManager;
    const dnf::PartyId partyId = partyManager.CreateParty(100).value();
    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);
    AddTestDungeon(dungeonCatalog);
    dnf::DungeonManager dungeonManager(
        partyManager,
        dungeonCatalog,
        enemyCatalog);

    const auto created = dungeonManager.CreateDungeon(partyId, 1001);
    const dnf::DungeonId dungeonId = created.dungeon->Id();
    assert(udpManager.Allocate(dungeonId, {100}).has_value());

    dnf::DungeonLifecycleService lifecycleService(
        dungeonManager,
        udpManager);
    assert(lifecycleService.CancelWaitingDungeon(dungeonId));

    assert(dungeonManager.FindDungeon(dungeonId) == nullptr);
    assert(dungeonManager.ActiveDungeonCount() == 0);
    assert(udpManager.AllocationCount() == 0);
    assert(!udpManager.FindPort(dungeonId).has_value());
}

void TestFinishRunningDungeonReleasesUdp()
{
    boost::asio::io_context ioContext;
    dnf::DungeonUdpManager udpManager(ioContext);
    dnf::PartyManager partyManager;
    const dnf::PartyId partyId = partyManager.CreateParty(100).value();
    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);
    AddTestDungeon(dungeonCatalog);
    dnf::DungeonManager dungeonManager(
        partyManager,
        dungeonCatalog,
        enemyCatalog);

    const auto created = dungeonManager.CreateDungeon(partyId, 1001);
    const dnf::DungeonId dungeonId = created.dungeon->Id();
    assert(udpManager.Allocate(dungeonId, {100}).has_value());
    assert(dungeonManager.StartDungeon(dungeonId));

    dnf::DungeonLifecycleService lifecycleService(
        dungeonManager,
        udpManager);
    assert(!lifecycleService.CancelWaitingDungeon(dungeonId));
    assert(lifecycleService.FinishDungeon(dungeonId));

    assert(dungeonManager.FindDungeon(dungeonId) == nullptr);
    assert(dungeonManager.ActiveDungeonCount() == 0);
    assert(udpManager.AllocationCount() == 0);
    assert(!lifecycleService.FinishDungeon(dungeonId));

    const auto retry = dungeonManager.CreateDungeon(partyId, 1001);
    assert(retry.status == dnf::CreateDungeonStatus::Success);
}
} // namespace

int main()
{
    TestCancelWaitingDungeonReleasesUdp();
    TestFinishRunningDungeonReleasesUdp();

    std::cout << "All dungeon lifecycle service tests passed.\n";
    return 0;
}
