#include "Packet.h"
#include "ReceiveBuffer.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
std::vector<std::uint8_t> MakeTestPacket(
    dnf::PacketType type,
    std::uint32_t requestId,
    const std::vector<std::uint8_t>& payload)
{
    dnf::PacketHeader header;
    header.packetSize = static_cast<std::uint16_t>(
        dnf::PACKET_HEADER_SIZE + payload.size());
    header.type = type;
    header.requestId = requestId;

    const auto headerBytes = dnf::EncodeHeader(header);
    std::vector<std::uint8_t> packetBytes(
        headerBytes.begin(), headerBytes.end());
    packetBytes.insert(packetBytes.end(), payload.begin(), payload.end());
    return packetBytes;
}

void TestSplitReceive()
{
    const auto packetBytes = MakeTestPacket(
        dnf::LoginRequest, 1, {'O', 'K'});

    const std::vector<std::uint8_t> firstPart(
        packetBytes.begin(), packetBytes.begin() + 5);
    const std::vector<std::uint8_t> secondPart(
        packetBytes.begin() + 5, packetBytes.end());

    dnf::ReceiveBuffer buffer;
    dnf::Packet packet;

    buffer.Append(firstPart);
    assert(buffer.TryPop(packet) == false);

    buffer.Append(secondPart);
    assert(buffer.TryPop(packet) == true);
    assert(packet.header.requestId == 1);
    assert(packet.payload == std::vector<std::uint8_t>({'O', 'K'}));
    assert(buffer.Size() == 0);
}

void TestCombinedReceive()
{
    std::vector<std::uint8_t> received = MakeTestPacket(
        dnf::ChannelListRequest, 10, {});
    const auto secondPacket = MakeTestPacket(
        dnf::JoinChannelRequest, 11, {0, 1});

    received.insert(
        received.end(), secondPacket.begin(), secondPacket.end());

    dnf::ReceiveBuffer buffer;
    dnf::Packet packet;
    buffer.Append(received);

    assert(buffer.TryPop(packet) == true);
    assert(packet.header.requestId == 10);

    assert(buffer.TryPop(packet) == true);
    assert(packet.header.requestId == 11);

    assert(buffer.TryPop(packet) == false);
}

void TestManyPacketsKeepUnreadBytesInOrder()
{
    constexpr std::uint32_t PACKET_COUNT = 700;
    std::vector<std::uint8_t> received;

    for (std::uint32_t requestId = 1;
         requestId <= PACKET_COUNT;
         ++requestId)
    {
        const auto packetBytes = MakeTestPacket(
            dnf::ChannelListRequest,
            requestId,
            {static_cast<std::uint8_t>(requestId & 0xFF)});
        received.insert(
            received.end(),
            packetBytes.begin(),
            packetBytes.end());
    }

    dnf::ReceiveBuffer buffer;
    buffer.Append(received);

    for (std::uint32_t requestId = 1;
         requestId <= PACKET_COUNT / 2;
         ++requestId)
    {
        dnf::Packet packet;
        assert(buffer.TryPop(packet));
        assert(packet.header.requestId == requestId);
    }

    const auto finalPacket = MakeTestPacket(
        dnf::ChannelListRequest,
        PACKET_COUNT + 1,
        {0xAB});
    buffer.Append(finalPacket);

    for (std::uint32_t requestId = PACKET_COUNT / 2 + 1;
         requestId <= PACKET_COUNT + 1;
         ++requestId)
    {
        dnf::Packet packet;
        assert(buffer.TryPop(packet));
        assert(packet.header.requestId == requestId);
    }

    assert(buffer.Size() == 0);
}

void TestInvalidPacketSize()
{
    // packetSize가 7이므로 8바이트 헤더보다 작다.
    const std::vector<std::uint8_t> invalidPacket = {
        0, 7, 0, 1, 0, 0, 0, 1};

    dnf::ReceiveBuffer buffer;
    dnf::Packet packet;
    buffer.Append(invalidPacket);

    bool errorOccurred = false;
    try
    {
        buffer.TryPop(packet);
    }
    catch (const std::runtime_error&)
    {
        errorOccurred = true;
    }

    assert(errorOccurred == true);
}
} // namespace

int main()
{
    TestSplitReceive();
    TestCombinedReceive();
    TestManyPacketsKeepUnreadBytesInOrder();
    TestInvalidPacketSize();

    std::cout << "All receive buffer tests passed.\n";
    return 0;
}
