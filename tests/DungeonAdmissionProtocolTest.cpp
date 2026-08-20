#include "DungeonAdmissionProtocol.h"
#include "Packet.h"
#include "ReceiveBuffer.h"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void TestEnterDungeonRequest()
{
    const auto payload = dnf::EncodeEnterDungeonRequestPayload(1001);
    const auto packetBytes = dnf::EncodePacket(
        dnf::EnterDungeonRequest,
        42,
        payload);

    dnf::ReceiveBuffer buffer;
    buffer.Append(packetBytes);

    dnf::Packet packet;
    assert(buffer.TryPop(packet));
    assert(packet.header.type == dnf::EnterDungeonRequest);
    assert(packet.header.requestId == 42);
    assert(dnf::DecodeEnterDungeonRequestPayload(packet.payload) == 1001);
}

void TestEnterDungeonResponse()
{
    const auto successPayload = dnf::EncodeEnterDungeonResponsePayload(
        dnf::EnterDungeonResult::Success,
        5001,
        40000,
        90001);
    const auto success =
        dnf::DecodeEnterDungeonResponsePayload(successPayload);

    assert(success.result == dnf::EnterDungeonResult::Success);
    assert(success.dungeonId == 5001);
    assert(success.udpPort == 40000);
    assert(success.udpToken == 90001);

    const auto failurePayload = dnf::EncodeEnterDungeonResponsePayload(
        dnf::EnterDungeonResult::NotPartyLeader,
        0,
        0,
        0);
    const auto failure =
        dnf::DecodeEnterDungeonResponsePayload(failurePayload);

    assert(failure.result == dnf::EnterDungeonResult::NotPartyLeader);
    assert(failure.dungeonId == 0);
    assert(failure.udpPort == 0);
    assert(failure.udpToken == 0);
}

void TestInvalidPayloads()
{
    bool invalidRequest = false;
    try
    {
        dnf::EncodeEnterDungeonRequestPayload(0);
    }
    catch (const std::invalid_argument&)
    {
        invalidRequest = true;
    }
    assert(invalidRequest);

    invalidRequest = false;
    try
    {
        dnf::DecodeEnterDungeonRequestPayload({0});
    }
    catch (const std::runtime_error&)
    {
        invalidRequest = true;
    }
    assert(invalidRequest);

    bool invalidResponse = false;
    try
    {
        dnf::EncodeEnterDungeonResponsePayload(
            dnf::EnterDungeonResult::Success,
            0,
            40000,
            90001);
    }
    catch (const std::invalid_argument&)
    {
        invalidResponse = true;
    }
    assert(invalidResponse);

    invalidResponse = false;
    try
    {
        dnf::DecodeEnterDungeonResponsePayload(
            dnf::EncodeEnterDungeonRequestPayload(1001));
    }
    catch (const std::runtime_error&)
    {
        invalidResponse = true;
    }
    assert(invalidResponse);

    invalidResponse = false;
    try
    {
        dnf::EncodeEnterDungeonResponsePayload(
            static_cast<dnf::EnterDungeonResult>(6),
            0,
            0,
            0);
    }
    catch (const std::invalid_argument&)
    {
        invalidResponse = true;
    }
    assert(invalidResponse);
}
} // namespace

int main()
{
    TestEnterDungeonRequest();
    TestEnterDungeonResponse();
    TestInvalidPayloads();

    std::cout << "All dungeon admission protocol tests passed.\n";
    return 0;
}
