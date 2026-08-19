#include "DungeonMessage_generated.h"

#include <flatbuffers/flatbuffer_builder.h>
#include <flatbuffers/verifier.h>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
void TestPlayerMovement()
{
    assert(static_cast<std::uint8_t>(
               Dnf::Protocol::DungeonPayload_PlayerMovement) == 1);

    flatbuffers::FlatBufferBuilder builder;

    const auto movement = Dnf::Protocol::CreatePlayerMovement(
        builder,
        42,
        1.0f,
        -1.0f,
        true);

    const auto message = Dnf::Protocol::CreateDungeonMessage(
        builder,
        1,
        5001,
        Dnf::Protocol::DungeonPayload_PlayerMovement,
        movement.Union());

    Dnf::Protocol::FinishDungeonMessageBuffer(builder, message);

    flatbuffers::Verifier verifier(
        builder.GetBufferPointer(),
        builder.GetSize());
    assert(Dnf::Protocol::VerifyDungeonMessageBuffer(verifier));

    const auto* received = Dnf::Protocol::GetDungeonMessage(
        builder.GetBufferPointer());
    assert(received->protocol_version() == 1);
    assert(received->dungeon_id() == 5001);

    const auto* receivedMovement = received->payload_as_PlayerMovement();
    assert(receivedMovement != nullptr);
    assert(receivedMovement->sequence() == 42);
    assert(receivedMovement->move_x() == 1.0f);
    assert(receivedMovement->move_y() == -1.0f);
    assert(receivedMovement->jump());

    const std::vector<std::uint8_t> truncatedBuffer(
        builder.GetBufferPointer(),
        builder.GetBufferPointer() + builder.GetSize() / 2);
    flatbuffers::Verifier invalidVerifier(
        truncatedBuffer.data(),
        truncatedBuffer.size());
    assert(!Dnf::Protocol::VerifyDungeonMessageBuffer(invalidVerifier));
}

void TestDungeonSnapshot()
{
    flatbuffers::FlatBufferBuilder builder;

    const Dnf::Protocol::Vec3 position(100.0f, 250.0f, 0.0f);
    const auto player = Dnf::Protocol::CreatePlayerSnapshot(
        builder,
        100,
        1,
        &position);

    const std::vector<flatbuffers::Offset<Dnf::Protocol::PlayerSnapshot>>
        playerOffsets = {player};
    const auto players = builder.CreateVector(playerOffsets);

    const Dnf::Protocol::Vec3 enemyPosition(500.0f, 250.0f, 0.0f);
    const auto enemy = Dnf::Protocol::CreateEnemySnapshot(
        builder,
        9001,
        2001,
        1,
        &enemyPosition,
        80,
        true);
    const std::vector<flatbuffers::Offset<Dnf::Protocol::EnemySnapshot>>
        enemyOffsets = {enemy};
    const auto enemies = builder.CreateVector(enemyOffsets);

    const auto snapshot = Dnf::Protocol::CreateDungeonSnapshot(
        builder,
        77,
        players,
        enemies);

    const auto message = Dnf::Protocol::CreateDungeonMessage(
        builder,
        1,
        5001,
        Dnf::Protocol::DungeonPayload_DungeonSnapshot,
        snapshot.Union());

    Dnf::Protocol::FinishDungeonMessageBuffer(builder, message);

    flatbuffers::Verifier verifier(
        builder.GetBufferPointer(),
        builder.GetSize());
    assert(Dnf::Protocol::VerifyDungeonMessageBuffer(verifier));

    const auto* received = Dnf::Protocol::GetDungeonMessage(
        builder.GetBufferPointer());
    const auto* receivedSnapshot = received->payload_as_DungeonSnapshot();

    assert(receivedSnapshot != nullptr);
    assert(receivedSnapshot->server_tick() == 77);
    assert(receivedSnapshot->players()->size() == 1);
    assert(receivedSnapshot->players()->Get(0)->session_id() == 100);
    assert(receivedSnapshot->players()->Get(0)->position()->x() == 100.0f);
    assert(receivedSnapshot->enemies()->size() == 1);
    assert(receivedSnapshot->enemies()->Get(0)->entity_id() == 9001);
    assert(receivedSnapshot->enemies()->Get(0)->current_hp() == 80);
}
} // namespace

int main()
{
    TestPlayerMovement();
    TestDungeonSnapshot();

    std::cout << "All FlatBuffers protocol tests passed.\n";
    return 0;
}
