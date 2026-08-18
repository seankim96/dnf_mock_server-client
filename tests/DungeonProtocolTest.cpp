#include "DungeonProtocol.h"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
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
} // namespace

int main()
{
    TestPlayerInputRoundTrip();
    TestBrokenBufferIsRejected();
    TestInvalidMovementIsRejected();

    std::cout << "All dungeon protocol tests passed.\n";
    return 0;
}
