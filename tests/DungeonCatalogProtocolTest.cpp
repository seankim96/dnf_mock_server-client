#include "DungeonCatalogProtocol.h"
#include "DungeonRoom.h"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
std::vector<dnf::DungeonTemplate> CreateTestDungeons()
{
    const dnf::RoomTemplate room{
        1, 1200.0f, 500.0f, {100.0f, 250.0f, 0.0f}};

    dnf::DungeonTemplate forest;
    forest.id = 1001;
    forest.name = "Forest";
    forest.rooms = {room};
    forest.recommendedPartySize = 2;
    forest.maxPartySize = 4;

    dnf::DungeonTemplate trainingRoom;
    trainingRoom.id = 1000;
    trainingRoom.name = "Training Room";
    trainingRoom.rooms = {room};
    trainingRoom.recommendedPartySize = 1;
    trainingRoom.maxPartySize = 1;

    return {trainingRoom, forest};
}

void TestCatalogPayload()
{
    const auto request = dnf::EncodeDungeonCatalogRequestPayload();
    assert(!request.empty());
    dnf::ValidateDungeonCatalogRequestPayload(request);

    const auto payload = dnf::EncodeDungeonCatalogResponsePayload(
        dnf::CatalogResult::Success,
        CreateTestDungeons());
    const auto response =
        dnf::DecodeDungeonCatalogResponsePayload(payload);

    assert(response.result == dnf::CatalogResult::Success);
    assert(response.dungeons.size() == 2);
    assert(response.dungeons[0].templateId == 1000);
    assert(response.dungeons[0].displayName == "Training Room");
    assert(response.dungeons[0].recommendedPartySize == 1);
    assert(response.dungeons[0].maxPartySize == 1);
    assert(response.dungeons[0].available);
    assert(response.dungeons[1].templateId == 1001);
    assert(response.dungeons[1].recommendedPartySize == 2);

    const auto unavailablePayload =
        dnf::EncodeDungeonCatalogResponsePayload(
            dnf::CatalogResult::Unavailable,
            {});
    const auto unavailable =
        dnf::DecodeDungeonCatalogResponsePayload(unavailablePayload);
    assert(unavailable.result == dnf::CatalogResult::Unavailable);
    assert(unavailable.dungeons.empty());
}

void TestInvalidCatalogPayload()
{
    bool threw = false;
    try
    {
        dnf::ValidateDungeonCatalogRequestPayload({1});
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    assert(threw);

    threw = false;
    try
    {
        dnf::DecodeDungeonCatalogResponsePayload(
            dnf::EncodeDungeonCatalogRequestPayload());
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    assert(threw);

    threw = false;
    try
    {
        dnf::EncodeDungeonCatalogResponsePayload(
            dnf::CatalogResult::Unavailable,
            CreateTestDungeons());
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    assert(threw);

    auto invalidDungeons = CreateTestDungeons();
    invalidDungeons[0].maxPartySize = 0;
    threw = false;
    try
    {
        dnf::EncodeDungeonCatalogResponsePayload(
            dnf::CatalogResult::Success,
            invalidDungeons);
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
    TestCatalogPayload();
    TestInvalidCatalogPayload();

    std::cout << "All dungeon catalog protocol tests passed.\n";
    return 0;
}
