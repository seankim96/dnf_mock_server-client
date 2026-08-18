#include "DungeonProtocol.h"
#include "DungeonMessage_generated.h"

#include <flatbuffers/verifier.h>

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
    return dungeon;
}

void TestPlayerInputRoundTrip()
{
    dnf::PlayerInputMessage sent;
    sent.dungeonId = 5001;
    sent.sequence = 42;
    sent.moveX = 1.0f;
    sent.moveY = -0.5f;
    sent.jump = true;

    const std::vector<std::uint8_t> bytes = dnf::EncodePlayerInput(sent);

    dnf::PlayerInputMessage received;
    assert(dnf::DecodePlayerInput(bytes, received));
    assert(received.dungeonId == sent.dungeonId);
    assert(received.sequence == sent.sequence);
    assert(received.moveX == sent.moveX);
    assert(received.moveY == sent.moveY);
    assert(received.jump == sent.jump);
}

void TestBrokenBufferIsRejected()
{
    dnf::PlayerInputMessage input;
    input.dungeonId = 5001;

    std::vector<std::uint8_t> bytes = dnf::EncodePlayerInput(input);
    bytes.resize(bytes.size() / 2);

    dnf::PlayerInputMessage output;
    assert(!dnf::DecodePlayerInput(bytes, output));
}

void TestInvalidMovementIsRejected()
{
    dnf::PlayerInputMessage input;
    input.dungeonId = 5001;
    input.moveX = 1.5f;

    bool threw = false;
    try
    {
        dnf::EncodePlayerInput(input);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }

    assert(threw);
}

void TestUdpHelloRoundTrip()
{
    dnf::UdpHelloMessage sent;
    sent.dungeonId = 5001;
    sent.sessionId = 100;
    sent.token = 90001;

    const auto bytes = dnf::EncodeUdpHello(sent);

    dnf::UdpHelloMessage received;
    assert(dnf::DecodeUdpHello(bytes, received));
    assert(received.dungeonId == sent.dungeonId);
    assert(received.sessionId == sent.sessionId);
    assert(received.token == sent.token);

    dnf::PlayerInputMessage input;
    input.dungeonId = 5001;
    const auto wrongMessageType = dnf::EncodePlayerInput(input);
    assert(!dnf::DecodeUdpHello(wrongMessageType, received));
}

void TestInvalidUdpHelloIsRejected()
{
    dnf::UdpHelloMessage hello;
    hello.dungeonId = 5001;
    hello.sessionId = 100;

    bool threw = false;
    try
    {
        dnf::EncodeUdpHello(hello);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }

    assert(threw);
}

void TestUdpHeartbeatRoundTrip()
{
    const dnf::UdpHeartbeatMessage sent{5001, 100};
    const auto bytes = dnf::EncodeUdpHeartbeat(sent);

    dnf::UdpHeartbeatMessage received;
    assert(dnf::DecodeUdpHeartbeat(bytes, received));
    assert(received.dungeonId == sent.dungeonId);
    assert(received.sessionId == sent.sessionId);
}

void TestDungeonSnapshotEncoding()
{
    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonInstance dungeon(
        5001,
        MakeDungeonTemplate(),
        20,
        {100, 200},
        enemyCatalog);

    const auto player = dungeon.FindPlayer(100);
    const auto room = dungeon.FindRoom(1);
    assert(player != nullptr);
    assert(room != nullptr);
    assert(player->MoveTo(*room, {300.0f, 200.0f, 0.0f}) ==
           dnf::MovePlayerResult::Success);

    const std::vector<std::uint8_t> bytes =
        dnf::EncodeDungeonSnapshot(dungeon, 77);

    flatbuffers::Verifier verifier(bytes.data(), bytes.size());
    assert(Dnf::Protocol::VerifyDungeonMessageBuffer(verifier));

    const Dnf::Protocol::DungeonMessage* message =
        Dnf::Protocol::GetDungeonMessage(bytes.data());
    assert(message->protocol_version() == dnf::DUNGEON_PROTOCOL_VERSION);
    assert(message->dungeon_id() == 5001);
    assert(message->payload_type() ==
           Dnf::Protocol::DungeonPayload_DungeonSnapshot);

    const Dnf::Protocol::DungeonSnapshot* snapshot =
        message->payload_as_DungeonSnapshot();
    assert(snapshot != nullptr);
    assert(snapshot->server_tick() == 77);
    assert(snapshot->players()->size() == 2);

    const Dnf::Protocol::PlayerSnapshot* firstPlayer =
        snapshot->players()->Get(0);
    assert(firstPlayer->session_id() == 100);
    assert(firstPlayer->room_id() == 1);
    assert(firstPlayer->position()->x() == 300.0f);
    assert(firstPlayer->position()->y() == 200.0f);
}
} // namespace

int main()
{
    TestPlayerInputRoundTrip();
    TestBrokenBufferIsRejected();
    TestInvalidMovementIsRejected();
    TestUdpHelloRoundTrip();
    TestInvalidUdpHelloIsRejected();
    TestUdpHeartbeatRoundTrip();
    TestDungeonSnapshotEncoding();

    std::cout << "All dungeon protocol tests passed.\n";
    return 0;
}
