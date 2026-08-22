#include "DungeonPlayerState.h"

#include <cassert>
#include <iostream>

namespace
{
dnf::RoomTemplate MakeRoom(dnf::RoomId roomId)
{
    dnf::RoomTemplate room;
    room.id = roomId;
    room.width = 1200.0f;
    room.depth = 500.0f;
    room.playerSpawn = {100.0f, 250.0f, 0.0f};

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

void TestMovementValidation()
{
    dnf::EnemyCatalog enemyCatalog;
    dnf::RoomState room(MakeRoom(1), enemyCatalog);
    dnf::DungeonPlayerState player(100, 1, {100.0f, 250.0f, 0.0f});

    assert(player.MoveTo(room, {200.0f, 250.0f, 0.0f}) ==
           dnf::MovePlayerResult::Success);
    assert(player.CurrentPosition().x == 200.0f);

    assert(player.MoveTo(room, {1300.0f, 250.0f, 0.0f}) ==
           dnf::MovePlayerResult::OutsideRoom);
    assert(player.MoveTo(room, {450.0f, 150.0f, 0.0f}) ==
           dnf::MovePlayerResult::BlockedByObstacle);
    assert(player.MoveTo(room, {640.0f, 340.0f, 0.0f}) ==
           dnf::MovePlayerResult::BlockedByObstacle);

    assert(room.ApplyObstacleDamage(2, 50));
    assert(player.MoveTo(room, {640.0f, 340.0f, 0.0f}) ==
           dnf::MovePlayerResult::Success);
}

void TestRoomChange()
{
    dnf::EnemyCatalog enemyCatalog;
    dnf::RoomState firstRoom(MakeRoom(1), enemyCatalog);
    dnf::RoomState secondRoom(MakeRoom(2), enemyCatalog);
    dnf::DungeonPlayerState player(100, 1, {100.0f, 250.0f, 0.0f});

    assert(player.MoveTo(secondRoom, {200.0f, 250.0f, 0.0f}) ==
           dnf::MovePlayerResult::WrongRoom);

    assert(player.EnterRoom(secondRoom, {100.0f, 250.0f, 0.0f}) ==
           dnf::MovePlayerResult::Success);
    assert(player.CurrentRoom() == 2);
    assert(player.CurrentPosition().x == 100.0f);

    assert(player.MoveTo(firstRoom, {200.0f, 250.0f, 0.0f}) ==
           dnf::MovePlayerResult::WrongRoom);
}

void TestManaAndSkillCooldown()
{
    dnf::DungeonPlayerState player(
        100,
        1,
        {100.0f, 250.0f, 0.0f},
        20);

    assert(player.CurrentMp() == 20);
    assert(player.BeginSkill(1001, 5, 2, 0, 1, 0) ==
           dnf::BeginSkillResult::Success);
    assert(player.CurrentMp() == 15);
    assert(player.RemainingCooldown(1001) == 2);

    assert(player.BeginSkill(1001, 5, 2, 0, 1, 0) ==
           dnf::BeginSkillResult::OnCooldown);
    assert(player.CurrentMp() == 15);

    player.AdvanceCombatTick();
    assert(player.RemainingCooldown(1001) == 1);
    player.AdvanceCombatTick();
    assert(player.RemainingCooldown(1001) == 0);

    assert(player.BeginSkill(1002, 16, 0, 0, 1, 0) ==
           dnf::BeginSkillResult::NotEnoughMana);
    assert(player.CurrentMp() == 15);

    assert(player.BeginSkill(1002, 15, 0, 0, 1, 0) ==
           dnf::BeginSkillResult::Success);
    assert(player.CurrentMp() == 0);

    assert(player.BeginSkill(0, 0, 0, 0, 1, 0) ==
           dnf::BeginSkillResult::InvalidSkill);

    const dnf::DungeonPlayerSnapshot snapshot = player.Snapshot();
    assert(snapshot.currentMp == 0);
    assert(snapshot.maxMp == 20);
}

void TestSkillActionPhases()
{
    dnf::DungeonPlayerState player(
        100,
        1,
        {100.0f, 250.0f, 0.0f});

    assert(player.BeginSkill(2001, 10, 10, 2, 1, 2) ==
           dnf::BeginSkillResult::Success);

    dnf::SkillActionSnapshot action = player.CurrentSkillAction();
    assert(action.skillId == 2001);
    assert(action.phase == dnf::SkillActionPhase::Startup);
    assert(action.remainingTicks == 2);

    assert(player.BeginSkill(2002, 10, 10, 1, 1, 1) ==
           dnf::BeginSkillResult::Busy);
    assert(player.CurrentMp() == 90);

    player.AdvanceCombatTick();
    action = player.CurrentSkillAction();
    assert(action.phase == dnf::SkillActionPhase::Startup);
    assert(action.remainingTicks == 1);

    player.AdvanceCombatTick();
    action = player.CurrentSkillAction();
    assert(action.phase == dnf::SkillActionPhase::Active);
    assert(action.remainingTicks == 1);

    player.AdvanceCombatTick();
    action = player.CurrentSkillAction();
    assert(action.phase == dnf::SkillActionPhase::Recovery);
    assert(action.remainingTicks == 2);

    player.AdvanceCombatTick();
    player.AdvanceCombatTick();
    action = player.CurrentSkillAction();
    assert(action.skillId == 0);
    assert(action.phase == dnf::SkillActionPhase::Idle);
    assert(action.remainingTicks == 0);
}

void TestHealthAndDeathRules()
{
    dnf::EnemyCatalog enemyCatalog;
    dnf::RoomState room(MakeRoom(1), enemyCatalog);
    dnf::RoomState nextRoom(MakeRoom(2), enemyCatalog);
    dnf::DungeonPlayerState player(
        100,
        1,
        {100.0f, 250.0f, 0.0f},
        50,
        120);

    assert(player.CurrentHp() == 120);
    assert(player.IsAlive());
    assert(!player.ApplyDamage(0));
    assert(player.ApplyDamage(30));
    assert(player.CurrentHp() == 90);

    assert(player.BeginSkill(3001, 10, 10, 3, 1, 2) ==
           dnf::BeginSkillResult::Success);
    assert(player.ApplyDamage(100));
    assert(player.CurrentHp() == 0);
    assert(!player.IsAlive());
    assert(!player.ApplyDamage(1));

    const dnf::DungeonPlayerSnapshot snapshot = player.Snapshot();
    assert(snapshot.currentHp == 0);
    assert(snapshot.maxHp == 120);
    assert(!snapshot.alive);
    assert(snapshot.skillAction.phase == dnf::SkillActionPhase::Idle);

    assert(player.MoveTo(room, {200.0f, 250.0f, 0.0f}) ==
           dnf::MovePlayerResult::Dead);
    assert(player.EnterRoom(nextRoom, {100.0f, 250.0f, 0.0f}) ==
           dnf::MovePlayerResult::Dead);
    assert(player.BeginSkill(3002, 10, 10, 1, 1, 1) ==
           dnf::BeginSkillResult::Dead);
}
} // namespace

int main()
{
    TestMovementValidation();
    TestRoomChange();
    TestManaAndSkillCooldown();
    TestSkillActionPhases();
    TestHealthAndDeathRules();

    std::cout << "All dungeon player state tests passed.\n";
    return 0;
}
