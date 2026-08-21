#include "AuthFlatBufferCodec.h"
#include "Packet.h"
#include "ReceiveBuffer.h"

#include <flatbuffers/flatbuffer_builder.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace protocol = Dnf::Protocol::Auth;

namespace
{
std::vector<std::uint8_t> MakeLoginPayload()
{
    flatbuffers::FlatBufferBuilder builder;
    const auto loginId = builder.CreateString("account_1");
    const auto password = builder.CreateString("test-password");
    const auto request =
        protocol::CreateLoginRequest(builder, loginId, password);

    return dnf::FinishAuthPayload(
        builder,
        protocol::AuthPayload_LoginRequest,
        request.Union());
}

void TestSplitLoginPacket()
{
    const auto packetBytes = dnf::EncodePacket(
        dnf::AuthLoginRequest,
        17,
        MakeLoginPayload());

    const auto splitPoint = packetBytes.begin() + 5;
    const std::vector<std::uint8_t> firstPart(
        packetBytes.begin(), splitPoint);
    const std::vector<std::uint8_t> secondPart(
        splitPoint, packetBytes.end());

    dnf::ReceiveBuffer receiveBuffer;
    dnf::Packet packet;

    receiveBuffer.Append(firstPart);
    assert(!receiveBuffer.TryPop(packet));

    receiveBuffer.Append(secondPart);
    assert(receiveBuffer.TryPop(packet));
    assert(packet.header.type == dnf::AuthLoginRequest);
    assert(packet.header.requestId == 17);

    const auto* message = dnf::DecodeAuthPayload(
        packet.payload,
        protocol::AuthPayload_LoginRequest);
    const auto* login = message->payload_as_LoginRequest();
    assert(login != nullptr);
    assert(login->login_id()->str() == "account_1");
    assert(login->password()->str() == "test-password");
}

void TestAllAuthPacketTypesAreAccepted()
{
    constexpr std::array<dnf::PacketType, 6> types = {
        dnf::AuthLoginRequest,
        dnf::AuthCharacterListRequest,
        dnf::AuthCharacterSelectionRequest,
        dnf::AuthLoginResponse,
        dnf::AuthCharacterListResponse,
        dnf::AuthCharacterSelectionResponse};

    for (const auto type : types)
    {
        const auto packetBytes = dnf::EncodePacket(type, 1, {});
        assert(packetBytes.size() == dnf::PACKET_HEADER_SIZE);
    }
}

void TestUnexpectedPayloadTypeIsRejected()
{
    bool rejected = false;

    try
    {
        dnf::DecodeAuthPayload(
            MakeLoginPayload(),
            protocol::AuthPayload_CharacterSelectionRequest);
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }

    assert(rejected);
}

void TestInvalidFlatBufferIsRejected()
{
    bool rejected = false;

    try
    {
        dnf::DecodeAuthPayload(
            {1, 2, 3, 4},
            protocol::AuthPayload_LoginRequest);
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }

    assert(rejected);
}

void TestUnsupportedVersionIsRejected()
{
    flatbuffers::FlatBufferBuilder builder;
    const auto request = protocol::CreateCharacterListRequest(builder);
    const auto message = protocol::CreateAuthMessage(
        builder,
        dnf::AUTH_PAYLOAD_PROTOCOL_VERSION + 1,
        protocol::AuthPayload_CharacterListRequest,
        request.Union());
    protocol::FinishAuthMessageBuffer(builder, message);

    const std::vector<std::uint8_t> bytes(
        builder.GetBufferPointer(),
        builder.GetBufferPointer() + builder.GetSize());

    bool rejected = false;

    try
    {
        dnf::DecodeAuthPayload(
            bytes,
            protocol::AuthPayload_CharacterListRequest);
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }

    assert(rejected);
}
} // namespace

int main()
{
    TestSplitLoginPacket();
    TestAllAuthPacketTypesAreAccepted();
    TestUnexpectedPayloadTypeIsRejected();
    TestInvalidFlatBufferIsRejected();
    TestUnsupportedVersionIsRejected();

    std::cout << "All auth FlatBuffer codec tests passed.\n";
    return 0;
}
