#include "DungeonCatalog.h"
#include "EnemyCatalog.h"

#include <cassert>
#include <iostream>

namespace
{
void AddTestEnemy(dnf::EnemyCatalog& catalog)
{
    dnf::EnemyTemplate enemy;
    enemy.id = 2001;
    enemy.name = "Goblin";
    enemy.maxHp = 100;
    enemy.moveSpeed = 120.0f;
    enemy.collision = {
        {-20.0f, -15.0f, 0.0f},
        {20.0f, 15.0f, 80.0f}};
    catalog.AddEnemy(enemy);
}

void TestAddAndGetDungeon()
{
    dnf::EnemyCatalog enemyCatalog;
    AddTestEnemy(enemyCatalog);
    dnf::DungeonCatalog catalog(enemyCatalog);

    dnf::RoomTemplate firstRoom{
        1, 1200.0f, 500.0f, {100.0f, 250.0f, 0.0f}};
    const dnf::RoomTemplate secondRoom{
        2, 1500.0f, 600.0f, {100.0f, 300.0f, 0.0f}};

    dnf::PortalTemplate portal;
    portal.id = 1;
    portal.triggerArea = {
        {1100.0f, 200.0f, 0.0f},
        {1200.0f, 300.0f, 200.0f}};
    portal.targetRoomId = 2;
    portal.targetPosition = {100.0f, 300.0f, 0.0f};
    portal.requiresRoomClear = true;
    firstRoom.portals.push_back(portal);

    dnf::ObstacleTemplate wall;
    wall.id = 1;
    wall.collision = {
        {400.0f, 100.0f, 0.0f},
        {500.0f, 200.0f, 200.0f}};
    firstRoom.obstacles.push_back(wall);

    dnf::ObstacleTemplate crate;
    crate.id = 2;
    crate.collision = {
        {700.0f, 300.0f, 0.0f},
        {780.0f, 380.0f, 100.0f}};
    crate.destructible = true;
    crate.maxHp = 100;
    firstRoom.obstacles.push_back(crate);

    dnf::EnemySpawnTemplate enemySpawn;
    enemySpawn.id = 1;
    enemySpawn.enemyTemplateId = 2001;
    enemySpawn.position = {800.0f, 250.0f, 0.0f};
    enemySpawn.wave = 1;
    firstRoom.enemySpawns.push_back(enemySpawn);

    assert(catalog.AddDungeon(
        1001,
        "Forest",
        {firstRoom, secondRoom},
        2,
        4));
    assert(catalog.AddDungeon(1000, "Training Room", {secondRoom}));
    assert(!catalog.AddDungeon(1001, "Duplicate", {firstRoom}));

    const auto dungeon = catalog.GetDungeon(1001);
    assert(dungeon.has_value());
    assert(dungeon->id == 1001);
    assert(dungeon->name == "Forest");
    assert(dungeon->recommendedPartySize == 2);
    assert(dungeon->maxPartySize == 4);
    assert(dungeon->rooms.size() == 2);
    assert(dungeon->rooms[0].id == 1);
    assert(dungeon->rooms[0].portals[0].targetRoomId == 2);
    assert(dungeon->rooms[0].obstacles.size() == 2);
    assert(dungeon->rooms[0].obstacles[1].maxHp == 100);
    assert(dungeon->rooms[0].enemySpawns[0].enemyTemplateId == 2001);
    assert(dungeon->rooms[1].playerSpawn.y == 300.0f);

    const auto dungeonList = catalog.GetDungeonList();
    assert(dungeonList.size() == 2);
    assert(dungeonList[0].id == 1000);
    assert(dungeonList[1].id == 1001);
    assert(dungeonList[1].recommendedPartySize == 2);
}

void TestInvalidDungeon()
{
    dnf::EnemyCatalog enemyCatalog;
    AddTestEnemy(enemyCatalog);
    dnf::DungeonCatalog catalog(enemyCatalog);

    const dnf::RoomTemplate validRoom{
        1, 1200.0f, 500.0f, {100.0f, 250.0f, 0.0f}};
    const dnf::RoomTemplate invalidSpawn{
        2, 1200.0f, 500.0f, {1300.0f, 250.0f, 0.0f}};

    dnf::RoomTemplate invalidPortalRoom = validRoom;
    dnf::PortalTemplate invalidPortal;
    invalidPortal.id = 1;
    invalidPortal.triggerArea = {
        {1100.0f, 200.0f, 0.0f},
        {1200.0f, 300.0f, 200.0f}};
    invalidPortal.targetRoomId = 999;
    invalidPortal.targetPosition = {100.0f, 250.0f, 0.0f};
    invalidPortalRoom.portals.push_back(invalidPortal);

    dnf::RoomTemplate invalidObstacleRoom = validRoom;
    dnf::ObstacleTemplate invalidObstacle;
    invalidObstacle.id = 1;
    invalidObstacle.collision = {
        {400.0f, 100.0f, 0.0f},
        {500.0f, 200.0f, 200.0f}};
    invalidObstacle.destructible = true;
    invalidObstacle.maxHp = 0;
    invalidObstacleRoom.obstacles.push_back(invalidObstacle);

    dnf::RoomTemplate invalidEnemyRoom = validRoom;
    dnf::EnemySpawnTemplate invalidEnemySpawn;
    invalidEnemySpawn.id = 1;
    invalidEnemySpawn.enemyTemplateId = 9999;
    invalidEnemySpawn.position = {800.0f, 250.0f, 0.0f};
    invalidEnemySpawn.wave = 1;
    invalidEnemyRoom.enemySpawns.push_back(invalidEnemySpawn);

    assert(!catalog.AddDungeon(0, "Forest", {validRoom}));
    assert(!catalog.AddDungeon(1001, "", {validRoom}));
    assert(!catalog.AddDungeon(1001, "Forest", {}));
    assert(!catalog.AddDungeon(1001, "Forest", {invalidSpawn}));
    assert(!catalog.AddDungeon(1001, "Forest", {validRoom, validRoom}));
    assert(!catalog.AddDungeon(1001, "Forest", {invalidPortalRoom}));
    assert(!catalog.AddDungeon(1001, "Forest", {invalidObstacleRoom}));
    assert(!catalog.AddDungeon(1001, "Forest", {invalidEnemyRoom}));
    assert(!catalog.AddDungeon(1001, "Forest", {validRoom}, 0, 4));
    assert(!catalog.AddDungeon(1001, "Forest", {validRoom}, 3, 2));
    assert(!catalog.AddDungeon(1001, "Forest", {validRoom}, 1, 5));
    assert(!catalog.GetDungeon(9999).has_value());
}
} // namespace

int main()
{
    TestAddAndGetDungeon();
    TestInvalidDungeon();

    std::cout << "All dungeon catalog tests passed.\n";
    return 0;
}
