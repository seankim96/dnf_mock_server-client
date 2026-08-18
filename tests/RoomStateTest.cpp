#include "RoomState.h"

#include <cassert>
#include <iostream>

namespace
{
dnf::EnemyTemplate MakeEnemy(
    dnf::EnemyTemplateId enemyId,
    std::uint32_t maxHp)
{
    dnf::EnemyTemplate enemy;
    enemy.id = enemyId;
    enemy.name = "Test Enemy";
    enemy.maxHp = maxHp;
    enemy.moveSpeed = 100.0f;
    enemy.collision = {
        {-20.0f, -15.0f, 0.0f},
        {20.0f, 15.0f, 80.0f}};
    return enemy;
}

dnf::RoomTemplate MakeRoom()
{
    dnf::RoomTemplate room;
    room.id = 1;
    room.width = 1200.0f;
    room.depth = 500.0f;
    room.playerSpawn = {100.0f, 250.0f, 0.0f};

    room.enemySpawns.push_back({1, 2001, {700.0f, 200.0f, 0.0f}, 1});
    room.enemySpawns.push_back({2, 2002, {900.0f, 300.0f, 0.0f}, 2});

    dnf::ObstacleTemplate wall;
    wall.id = 1;
    wall.collision = {
        {400.0f, 100.0f, 0.0f},
        {500.0f, 200.0f, 200.0f}};
    room.obstacles.push_back(wall);

    dnf::ObstacleTemplate crate;
    crate.id = 2;
    crate.collision = {
        {600.0f, 300.0f, 0.0f},
        {680.0f, 380.0f, 100.0f}};
    crate.destructible = true;
    crate.maxHp = 50;
    room.obstacles.push_back(crate);

    return room;
}

void TestEnemyWaves()
{
    dnf::EnemyCatalog enemyCatalog;
    enemyCatalog.AddEnemy(MakeEnemy(2001, 100));
    enemyCatalog.AddEnemy(MakeEnemy(2002, 200));

    dnf::RoomState room(MakeRoom(), enemyCatalog);
    assert(room.CurrentWave() == 0);
    assert(!room.IsCleared());

    assert(room.StartNextWave());
    assert(room.CurrentWave() == 1);

    auto enemies = room.Enemies();
    assert(enemies.size() == 1);
    assert(enemies[0].currentHp == 100);
    assert(!room.StartNextWave());

    assert(room.ApplyEnemyDamage(enemies[0].entityId, 40));
    assert(room.Enemies()[0].currentHp == 60);
    assert(room.ApplyEnemyDamage(enemies[0].entityId, 60));
    assert(!room.Enemies()[0].alive);

    assert(room.StartNextWave());
    assert(room.CurrentWave() == 2);
    enemies = room.Enemies();
    assert(enemies.size() == 2);
    assert(room.ApplyEnemyDamage(enemies[1].entityId, 200));
    assert(room.IsCleared());
    assert(!room.StartNextWave());
}

void TestObstacleDamage()
{
    dnf::EnemyCatalog enemyCatalog;
    dnf::RoomState room(MakeRoom(), enemyCatalog);

    assert(!room.ApplyObstacleDamage(1, 10));
    assert(room.ApplyObstacleDamage(2, 20));
    assert(room.Obstacles()[1].currentHp == 30);
    assert(room.ApplyObstacleDamage(2, 30));
    assert(room.Obstacles()[1].destroyed);
    assert(!room.ApplyObstacleDamage(2, 10));
}
} // namespace

int main()
{
    TestEnemyWaves();
    TestObstacleDamage();

    std::cout << "All room state tests passed.\n";
    return 0;
}
