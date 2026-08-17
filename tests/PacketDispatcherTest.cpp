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
    dnf::Packet request;
    request.header.type = dnf::LoginRequest;
    request.header.requestId = 42;
    request.payload = {'M', 'o', 'c', 'k'};

    dnf::PacketDispatcher dispatcher;
    const auto responseBytes = dispatcher.Dispatch(request);

    dnf::ReceiveBuffer buffer;
    buffer.Append(responseBytes);

    dnf::Packet response;
    assert(buffer.TryPop(response) == true);
    assert(response.header.type == dnf::LoginResponse);
    assert(response.header.requestId == 42);
    assert(response.payload == std::vector<std::uint8_t>({0}));
}

void TestMissingHandler()
{
    dnf::Packet request;
    request.header.type = dnf::ChannelListRequest;

    dnf::PacketDispatcher dispatcher;
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

void TestInvalidLoginRequest()
{
    dnf::Packet request;
    request.header.type = dnf::LoginRequest;
    request.header.requestId = 43;
    request.payload = {};

    dnf::PacketDispatcher dispatcher;
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
    TestMissingHandler();

    std::cout << "All packet dispatcher tests passed.\n";
    return 0;
}
