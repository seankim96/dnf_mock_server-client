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

void TestPortalRequiresRoomClear()
{
    dnf::EnemyCatalog enemyCatalog;
    dnf::EnemyTemplate enemy;
    enemy.id = 2001;
    enemy.name = "Goblin";
    enemy.maxHp = 100;
    enemy.collision = {
        {-20.0f, -15.0f, 0.0f},
        {20.0f, 15.0f, 80.0f}};
    assert(enemyCatalog.AddEnemy(enemy));

    dnf::DungeonTemplate dungeonTemplate = MakeDungeonTemplate();

    dnf::PortalTemplate portal;
    portal.id = 1;
    portal.triggerArea = {
        {90.0f, 240.0f, 0.0f},
        {110.0f, 260.0f, 100.0f}};
    portal.targetRoomId = 2;
    portal.targetPosition = {200.0f, 300.0f, 0.0f};
    portal.requiresRoomClear = true;
    dungeonTemplate.rooms[0].portals.push_back(portal);

    dnf::EnemySpawnTemplate spawn;
    spawn.id = 1;
    spawn.enemyTemplateId = enemy.id;
    spawn.position = {500.0f, 250.0f, 0.0f};
    spawn.wave = 1;
    dungeonTemplate.rooms[0].enemySpawns.push_back(spawn);

    spawn.id = 2;
    spawn.position = {550.0f, 250.0f, 0.0f};
    spawn.wave = 2;
    dungeonTemplate.rooms[0].enemySpawns.push_back(spawn);

    spawn.id = 3;
    spawn.position = {600.0f, 300.0f, 0.0f};
    spawn.wave = 1;
    dungeonTemplate.rooms[1].enemySpawns.push_back(spawn);

    dnf::DungeonInstance dungeon(
        10,
        dungeonTemplate,
        20,
        {100},
        enemyCatalog);

    assert(dungeon.TryUsePortal(100) ==
           dnf::UsePortalResult::DungeonNotRunning);
    assert(dungeon.Start());
    assert(dungeon.TryUsePortal(999) ==
           dnf::UsePortalResult::PlayerNotFound);
    assert(dungeon.TryUsePortal(100) ==
           dnf::UsePortalResult::RoomNotCleared);

    const auto firstRoom = dungeon.FindRoom(1);
    const auto secondRoom = dungeon.FindRoom(2);
    assert(firstRoom->CurrentWave() == 1);
    assert(secondRoom->CurrentWave() == 0);
    const auto enemies = firstRoom->Enemies();
    assert(enemies.size() == 1);
    assert(firstRoom->ApplyEnemyDamage(enemies[0].entityId, 100));

    assert(dungeon.TryUsePortal(100) ==
           dnf::UsePortalResult::RoomNotCleared);
    assert(dungeon.AdvanceRoomWaves() == 1);
    assert(firstRoom->CurrentWave() == 2);
    assert(secondRoom->CurrentWave() == 0);

    const auto secondWaveEnemies = firstRoom->Enemies();
    assert(secondWaveEnemies.size() == 2);
    assert(firstRoom->ApplyEnemyDamage(
        secondWaveEnemies[1].entityId,
        100));

    assert(dungeon.TryUsePortal(100) == dnf::UsePortalResult::Success);

    const auto player = dungeon.FindPlayer(100);
    assert(player->CurrentRoom() == 2);
    assert(player->CurrentPosition().x == 200.0f);
    assert(player->CurrentPosition().y == 300.0f);

    assert(secondRoom->CurrentWave() == 1);
    assert(secondRoom->Enemies().size() == 1);
    assert(dungeon.AdvanceRoomWaves() == 0);
    assert(!dungeon.TryFinishIfCleared());

    const auto finalEnemies = secondRoom->Enemies();
    assert(secondRoom->ApplyEnemyDamage(
        finalEnemies[0].entityId,
        100));
    assert(dungeon.TryFinishIfCleared());
    assert(dungeon.State() == dnf::DungeonState::Finished);
    assert(dungeon.TryUsePortal(100) ==
           dnf::UsePortalResult::DungeonNotRunning);
}
} // namespace

int main()
{
    TestDungeonInformation();
    TestDungeonState();
    TestInvalidParticipantCount();
    TestPortalRequiresRoomClear();

    std::cout << "All dungeon instance tests passed.\n";
    return 0;
}
