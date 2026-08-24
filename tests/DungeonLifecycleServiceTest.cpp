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

void TestReconnectRotatesUdpIdentityAndPreservesDungeonState()
{
    boost::asio::io_context ioContext;
    dnf::DungeonUdpManager udpManager(ioContext);
    dnf::PartyManager partyManager;
    const dnf::PartyId partyId = partyManager.CreateParty(100).value();
    assert(partyManager.JoinParty(partyId, 200) ==
           dnf::JoinPartyResult::Success);
    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);
    AddTestDungeon(dungeonCatalog);
    dnf::DungeonManager dungeonManager(
        partyManager,
        dungeonCatalog,
        enemyCatalog);
    dnf::DungeonLifecycleService lifecycleService(
        dungeonManager,
        udpManager);

    assert(lifecycleService.RegisterPlayerSession(100, 5001).status ==
           dnf::RegisterDungeonSessionStatus::Registered);
    assert(lifecycleService.RegisterPlayerSession(200, 5002).status ==
           dnf::RegisterDungeonSessionStatus::Registered);

    const auto created = dungeonManager.CreateDungeon(partyId, 1001);
    const dnf::DungeonId dungeonId = created.dungeon->Id();
    assert(udpManager.Allocate(
        dungeonId,
        created.dungeon->Participants()).has_value());

    const auto originalPlayer = created.dungeon->FindPlayer(100);
    assert(originalPlayer->ApplyDamage(20));
    const dnf::DungeonUdpToken oldToken =
        udpManager.FindToken(dungeonId, 100).value();

    assert(lifecycleService.DisconnectPlayerSession(100));
    assert(created.dungeon->IsParticipantDisconnected(100));

    const auto reconnected =
        lifecycleService.RegisterPlayerSession(300, 5001);
    assert(reconnected.status ==
           dnf::RegisterDungeonSessionStatus::Reconnected);
    assert(reconnected.dungeonId == dungeonId);
    assert(reconnected.replacedSessionId == 100);
    assert(reconnected.freshUdpToken.has_value());
    assert(reconnected.freshUdpToken.value() != oldToken);
    assert(!udpManager.FindToken(dungeonId, 100).has_value());
    assert(udpManager.FindToken(dungeonId, 300) ==
           reconnected.freshUdpToken);
    assert(created.dungeon->FindPlayer(300) == originalPlayer);
    assert(created.dungeon->FindPlayer(300)->CurrentHp() == 80);
    assert(dungeonManager.FindDungeonByParticipant(300) ==
           created.dungeon);
}

void TestAbandonmentRemovesPlayersAndReleasesEmptyDungeon()
{
    boost::asio::io_context ioContext;
    dnf::DungeonUdpManager udpManager(ioContext);
    dnf::PartyManager partyManager;
    const dnf::PartyId partyId = partyManager.CreateParty(100).value();
    assert(partyManager.JoinParty(partyId, 200) ==
           dnf::JoinPartyResult::Success);
    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);
    AddTestDungeon(dungeonCatalog);
    dnf::DungeonManager dungeonManager(
        partyManager,
        dungeonCatalog,
        enemyCatalog);
    dnf::DungeonLifecycleService lifecycleService(
        dungeonManager,
        udpManager);

    lifecycleService.RegisterPlayerSession(100, 5001);
    lifecycleService.RegisterPlayerSession(200, 5002);
    const auto created = dungeonManager.CreateDungeon(partyId, 1001);
    const dnf::DungeonId dungeonId = created.dungeon->Id();
    assert(udpManager.Allocate(
        dungeonId,
        created.dungeon->Participants()).has_value());

    assert(lifecycleService.DisconnectPlayerSession(100));
    const auto firstSweep = lifecycleService.SweepAbandonedParticipants(
        std::chrono::steady_clock::now() + std::chrono::seconds(31),
        std::chrono::seconds(30),
        std::chrono::hours(2));
    assert(firstSweep.removedParticipants.size() == 1);
    assert(firstSweep.releasedDungeons.empty());
    assert(dungeonManager.FindDungeon(dungeonId) != nullptr);
    assert(!udpManager.FindToken(dungeonId, 100).has_value());
    assert(udpManager.FindToken(dungeonId, 200).has_value());

    assert(lifecycleService.DisconnectPlayerSession(200));
    const auto secondSweep = lifecycleService.SweepAbandonedParticipants(
        std::chrono::steady_clock::now() + std::chrono::seconds(31),
        std::chrono::seconds(30),
        std::chrono::hours(2));
    assert(secondSweep.removedParticipants.size() == 1);
    assert(secondSweep.releasedDungeons ==
           std::vector<dnf::DungeonId>({dungeonId}));
    assert(dungeonManager.FindDungeon(dungeonId) == nullptr);
    assert(udpManager.AllocationCount() == 0);
}

void TestMaxDungeonLifetimeReleasesResources()
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
    dnf::DungeonLifecycleService lifecycleService(
        dungeonManager,
        udpManager);

    lifecycleService.RegisterPlayerSession(100, 5001);
    const auto created = dungeonManager.CreateDungeon(partyId, 1001);
    const dnf::DungeonId dungeonId = created.dungeon->Id();
    assert(udpManager.Allocate(dungeonId, {100}).has_value());

    const auto sweep = lifecycleService.SweepAbandonedParticipants(
        created.dungeon->CreatedAt() + std::chrono::hours(2),
        std::chrono::seconds(30),
        std::chrono::hours(2));
    assert(sweep.releasedDungeons ==
           std::vector<dnf::DungeonId>({dungeonId}));
    assert(dungeonManager.FindDungeon(dungeonId) == nullptr);
    assert(udpManager.AllocationCount() == 0);
}

void TestDuplicateActivePlayerLoginIsRejected()
{
    boost::asio::io_context ioContext;
    dnf::DungeonUdpManager udpManager(ioContext);
    dnf::PartyManager partyManager;
    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);
    AddTestDungeon(dungeonCatalog);
    dnf::DungeonManager dungeonManager(
        partyManager,
        dungeonCatalog,
        enemyCatalog);
    dnf::DungeonLifecycleService lifecycleService(
        dungeonManager,
        udpManager);

    assert(lifecycleService.RegisterPlayerSession(100, 5001).status ==
           dnf::RegisterDungeonSessionStatus::Registered);
    assert(lifecycleService.RegisterPlayerSession(300, 5001).status ==
           dnf::RegisterDungeonSessionStatus::InvalidIdentity);
}

void TestUdpRotationFailureRollsBackSessionRebind()
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
    dnf::DungeonLifecycleService lifecycleService(
        dungeonManager,
        udpManager);

    lifecycleService.RegisterPlayerSession(100, 5001);
    const auto created = dungeonManager.CreateDungeon(partyId, 1001);
    assert(lifecycleService.DisconnectPlayerSession(100));

    // UDP 할당이 없는 상태라 token 회전은 실패해야 한다.
    const auto reconnect =
        lifecycleService.RegisterPlayerSession(300, 5001);
    assert(reconnect.status ==
           dnf::RegisterDungeonSessionStatus::Reconnected);
    assert(!reconnect.freshUdpToken.has_value());
    assert(created.dungeon->HasParticipant(100));
    assert(created.dungeon->IsParticipantDisconnected(100));
    assert(!created.dungeon->HasParticipant(300));
    assert(created.dungeon->FindPlayer(100)->Session() == 100);

    // 실패한 새 세션은 active identity를 점유하지 않는다.
    const auto retry = lifecycleService.RegisterPlayerSession(400, 5001);
    assert(retry.status ==
           dnf::RegisterDungeonSessionStatus::Reconnected);
    assert(!retry.freshUdpToken.has_value());
    assert(created.dungeon->HasParticipant(100));
}
} // namespace

int main()
{
    TestCancelWaitingDungeonReleasesUdp();
    TestFinishRunningDungeonReleasesUdp();
    TestReconnectRotatesUdpIdentityAndPreservesDungeonState();
    TestAbandonmentRemovesPlayersAndReleasesEmptyDungeon();
    TestMaxDungeonLifetimeReleasesResources();
    TestDuplicateActivePlayerLoginIsRejected();
    TestUdpRotationFailureRollsBackSessionRebind();

    std::cout << "All dungeon lifecycle service tests passed.\n";
    return 0;
}
