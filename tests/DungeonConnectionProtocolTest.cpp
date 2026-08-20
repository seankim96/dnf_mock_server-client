#include "DungeonConnectionProtocol.h"
#include "Packet.h"
#include "ReceiveBuffer.h"

#include <cassert>
#include <iostream>
#include <stdexcept>

namespace
{
void TestConnectionInfoPacket()
{
    const auto requestPayload =
        dnf::EncodeDungeonConnectionInfoRequestPayload();
    assert(!requestPayload.empty());

    const auto packetBytes = dnf::EncodePacket(
        dnf::DungeonConnectionInfoRequest,
        60,
        requestPayload);

    dnf::ReceiveBuffer buffer;
    buffer.Append(packetBytes);

    dnf::Packet request;
    assert(buffer.TryPop(request));
    assert(request.header.type == dnf::DungeonConnectionInfoRequest);
    dnf::ValidateDungeonConnectionInfoRequestPayload(request.payload);

    const auto responsePayload =
        dnf::EncodeDungeonConnectionInfoResponsePayload(
            dnf::DungeonConnectionInfoResult::Success,
            5001,
            40000,
            90001);
    const auto response =
        dnf::DecodeDungeonConnectionInfoResponsePayload(responsePayload);

    assert(response.result == dnf::DungeonConnectionInfoResult::Success);
    assert(response.dungeonId == 5001);
    assert(response.udpPort == 40000);
    assert(response.udpToken == 90001);

    const auto failurePayload =
        dnf::EncodeDungeonConnectionInfoResponsePayload(
            dnf::DungeonConnectionInfoResult::NotInParty,
            0,
            0,
            0);
    const auto failure =
        dnf::DecodeDungeonConnectionInfoResponsePayload(failurePayload);
    assert(failure.result ==
           dnf::DungeonConnectionInfoResult::NotInParty);
}

void TestInvalidConnectionInfo()
{
    bool invalidRequest = false;
    try
    {
        dnf::ValidateDungeonConnectionInfoRequestPayload({1});
    }
    catch (const std::runtime_error&)
    {
        invalidRequest = true;
    }
    assert(invalidRequest);

    bool invalidResponse = false;
    try
    {
        dnf::EncodeDungeonConnectionInfoResponsePayload(
            dnf::DungeonConnectionInfoResult::Success,
            5001,
            0,
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
        dnf::DecodeDungeonConnectionInfoResponsePayload(
            dnf::EncodeDungeonConnectionInfoRequestPayload());
    }
    catch (const std::runtime_error&)
    {
        invalidResponse = true;
    }
    assert(invalidResponse);

    invalidResponse = false;
    try
    {
        dnf::EncodeDungeonConnectionInfoResponsePayload(
            static_cast<dnf::DungeonConnectionInfoResult>(5),
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
    TestConnectionInfoPacket();
    TestInvalidConnectionInfo();

    std::cout << "All dungeon connection protocol tests passed.\n";
    return 0;
}
