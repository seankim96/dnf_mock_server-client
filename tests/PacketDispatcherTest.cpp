#include "ChannelProtocol.h"
#include "PacketDispatcher.h"
#include "ReceiveBuffer.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void TestLoginRequest()
{
    dnf::ChannelManager channelManager;
    dnf::Packet request;
    request.header.type = dnf::LoginRequest;
    request.header.requestId = 42;
    request.payload = {'M', 'o', 'c', 'k'};

    dnf::PacketDispatcher dispatcher(channelManager, 100);
    const auto responseBytes = dispatcher.Dispatch(request);

    dnf::ReceiveBuffer buffer;
    buffer.Append(responseBytes);

    dnf::Packet response;
    assert(buffer.TryPop(response) == true);
    assert(response.header.type == dnf::LoginResponse);
    assert(response.header.requestId == 42);
    assert(response.payload == std::vector<std::uint8_t>({0}));
}

void TestChannelListRequest()
{
    dnf::ChannelManager channelManager;
    channelManager.AddChannel(1, "Channel 1", 100);
    channelManager.AddChannel(2, "Channel 2", 200);
    channelManager.JoinChannel(500, 1);

    dnf::Packet request;
    request.header.type = dnf::ChannelListRequest;
    request.header.requestId = 44;

    dnf::PacketDispatcher dispatcher(channelManager, 100);
    const auto responseBytes = dispatcher.Dispatch(request);

    dnf::ReceiveBuffer buffer;
    buffer.Append(responseBytes);

    dnf::Packet response;
    assert(buffer.TryPop(response) == true);
    assert(response.header.type == dnf::ChannelListResponse);
    assert(response.header.requestId == 44);

    const auto channels = dnf::DecodeChannelListPayload(response.payload);
    assert(channels.size() == 2);
    assert(channels[0].id == 1);
    assert(channels[0].currentPlayers == 1);
    assert(channels[0].maxPlayers == 100);
    assert(channels[1].id == 2);
    assert(channels[1].currentPlayers == 0);
    assert(channels[1].maxPlayers == 200);
}

void TestMissingHandler()
{
    dnf::ChannelManager channelManager;
    dnf::Packet request;
    request.header.type = dnf::LoginResponse;

    dnf::PacketDispatcher dispatcher(channelManager, 100);
    bool errorOccurred = false;

    try
    {
        dispatcher.Dispatch(request);
    }
    catch (const std::runtime_error&)
    {
        errorOccurred = true;
    }

    assert(errorOccurred == true);
}

void TestJoinChannelRequest()
{
    dnf::ChannelManager channelManager;
    channelManager.AddChannel(1, "Channel 1", 100);

    dnf::Packet request;
    request.header.type = dnf::JoinChannelRequest;
    request.header.requestId = 45;
    request.payload = dnf::EncodeJoinChannelRequestPayload(1);

    dnf::PacketDispatcher dispatcher(channelManager, 700);
    const auto responseBytes = dispatcher.Dispatch(request);

    dnf::ReceiveBuffer buffer;
    buffer.Append(responseBytes);

    dnf::Packet response;
    assert(buffer.TryPop(response) == true);
    assert(response.header.type == dnf::JoinChannelResponse);
    assert(response.header.requestId == 45);

    const auto result =
        dnf::DecodeJoinChannelResponsePayload(response.payload);
    assert(result.result == dnf::JoinChannelResult::Success);
    assert(result.channelId == 1);
    assert(channelManager.GetJoinedChannel(700).value() == 1);
}

void TestInvalidLoginRequest()
{
    dnf::ChannelManager channelManager;
    dnf::Packet request;
    request.header.type = dnf::LoginRequest;
    request.header.requestId = 43;
    request.payload = {};

    dnf::PacketDispatcher dispatcher(channelManager, 100);
    const auto responseBytes = dispatcher.Dispatch(request);

    dnf::ReceiveBuffer buffer;
    buffer.Append(responseBytes);

    dnf::Packet response;
    assert(buffer.TryPop(response) == true);
    assert(response.payload == std::vector<std::uint8_t>({1}));
}
} // namespace

int main()
{
    TestLoginRequest();
    TestInvalidLoginRequest();
    TestChannelListRequest();
    TestJoinChannelRequest();
    TestMissingHandler();

    std::cout << "All packet dispatcher tests passed.\n";
    return 0;
}
