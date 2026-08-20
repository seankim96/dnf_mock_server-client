#include "DungeonStaticDataProtocol.h"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
dnf::EnemyTemplate MakeGoblin()
{
    dnf::EnemyTemplate goblin;
    goblin.id = 2001;
    goblin.name = "Goblin";
    goblin.maxHp = 100;
    goblin.moveSpeed = 120.0f;
    goblin.aiType = dnf::EnemyAiType::Melee;
    goblin.collision = {
        {-20.0f, -15.0f, 0.0f},
        {20.0f, 15.0f, 80.0f}};
    return goblin;
}

std::vector<dnf::RoomTemplate> MakeRooms()
{
    dnf::RoomTemplate firstRoom{
        1, 1200.0f, 500.0f, {100.0f, 250.0f, 0.0f}};
    dnf::RoomTemplate secondRoom{
        2, 1500.0f, 600.0f, {100.0f, 300.0f, 0.0f}};

    dnf::PortalTemplate portal;
    portal.id = 1;
    portal.triggerArea = {
        {1100.0f, 200.0f, 0.0f},
        {1200.0f, 300.0f, 200.0f}};
    portal.targetRoomId = 2;
    portal.targetPosition = secondRoom.playerSpawn;
    firstRoom.portals.push_back(portal);

    dnf::ObstacleTemplate crate;
    crate.id = 1;
    crate.collision = {
        {500.0f, 100.0f, 0.0f},
        {580.0f, 180.0f, 100.0f}};
    crate.destructible = true;
    crate.maxHp = 100;
    firstRoom.obstacles.push_back(crate);

    dnf::EnemySpawnTemplate spawn;
    spawn.id = 1;
    spawn.enemyTemplateId = 2001;
    spawn.position = {800.0f, 250.0f, 0.0f};
    spawn.wave = 1;
    firstRoom.enemySpawns.push_back(spawn);

    return {firstRoom, secondRoom};
}

void TestDungeonStaticDataPayload()
{
    const auto request =
        dnf::EncodeDungeonStaticDataRequestPayload(5001);
    assert(dnf::DecodeDungeonStaticDataRequestPayload(request) == 5001);

    const auto payload = dnf::EncodeDungeonStaticDataResponsePayload(
        dnf::DungeonStaticDataResult::Success,
        5001,
        1001,
        MakeRooms(),
        {MakeGoblin()});
    const auto response =
        dnf::DecodeDungeonStaticDataResponsePayload(payload);

    assert(response.result == dnf::DungeonStaticDataResult::Success);
    assert(response.dungeonId == 5001);
    assert(response.dungeonTemplateId == 1001);
    assert(response.rooms.size() == 2);
    assert(response.rooms[0].portals[0].targetRoomId == 2);
    assert(response.rooms[0].obstacles[0].destructible);
    assert(response.rooms[0].enemySpawns[0].enemyTemplateId == 2001);
    assert(response.enemyTemplates.size() == 1);
    assert(response.enemyTemplates[0].name == "Goblin");
    assert(response.enemyTemplates[0].maxHp == 100);

    const auto failurePayload =
        dnf::EncodeDungeonStaticDataResponsePayload(
            dnf::DungeonStaticDataResult::DungeonNotFound,
            0,
            0,
            {},
            {});
    const auto failure =
        dnf::DecodeDungeonStaticDataResponsePayload(failurePayload);
    assert(failure.result ==
           dnf::DungeonStaticDataResult::DungeonNotFound);
    assert(failure.rooms.empty());
}

void TestInvalidDungeonStaticData()
{
    bool threw = false;
    try
    {
        dnf::EncodeDungeonStaticDataRequestPayload(0);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    assert(threw);

    threw = false;
    try
    {
        dnf::DecodeDungeonStaticDataResponsePayload(
            dnf::EncodeDungeonStaticDataRequestPayload(1));
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    assert(threw);

    threw = false;
    try
    {
        dnf::EncodeDungeonStaticDataResponsePayload(
            dnf::DungeonStaticDataResult::Success,
            0,
            1001,
            MakeRooms(),
            {MakeGoblin()});
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    assert(threw);

    auto rooms = MakeRooms();
    rooms[0].enemySpawns[0].enemyTemplateId = 9999;
    threw = false;
    try
    {
        dnf::EncodeDungeonStaticDataResponsePayload(
            dnf::DungeonStaticDataResult::Success,
            5001,
            1001,
            rooms,
            {MakeGoblin()});
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    assert(threw);
}
} // namespace

int main()
{
    TestDungeonStaticDataPayload();
    TestInvalidDungeonStaticData();

    std::cout << "All dungeon static data protocol tests passed.\n";
    return 0;
}
