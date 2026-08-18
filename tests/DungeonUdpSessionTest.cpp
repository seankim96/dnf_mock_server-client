#include "DungeonProtocol.h"
#include "DungeonUdpManager.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/system/error_code.hpp>

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

namespace
{
void TestUdpHelloRegistersEndpoint()
{
    using boost::asio::ip::udp;

    boost::asio::io_context serverIoContext;
    dnf::DungeonUdpManager manager(serverIoContext);

    const auto port = manager.Allocate(5001, {100});
    const auto token = manager.FindToken(5001, 100);
    assert(port.has_value());
    assert(token.has_value());

    boost::asio::io_context clientIoContext;
    udp::socket firstClient(clientIoContext);
    udp::socket secondClient(clientIoContext);
    firstClient.open(udp::v4());
    secondClient.open(udp::v4());
    firstClient.bind(udp::endpoint(udp::v4(), 0));
    secondClient.bind(udp::endpoint(udp::v4(), 0));

    const udp::endpoint serverEndpoint(
        boost::asio::ip::address_v4::loopback(),
        port.value());

    dnf::UdpHelloMessage hello;
    hello.dungeonId = 5001;
    hello.sessionId = 100;
    hello.token = token.value() ^ 1;
    if (hello.token == 0)
    {
        hello.token = 2;
    }
    const auto wrongHello = dnf::EncodeUdpHello(hello);

    hello.token = token.value();
    const auto validHello = dnf::EncodeUdpHello(hello);

    std::thread serverThread(
        [&serverIoContext]
        {
            serverIoContext.run();
        });

    boost::system::error_code firstSendError;
    firstClient.send_to(
        boost::asio::buffer(wrongHello),
        serverEndpoint,
        0,
        firstSendError);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const bool wrongTokenRejected =
        !manager.FindEndpoint(5001, 100).has_value();

    boost::system::error_code validSendError;
    firstClient.send_to(
        boost::asio::buffer(validHello),
        serverEndpoint,
        0,
        validSendError);

    std::optional<udp::endpoint> registeredEndpoint;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        registeredEndpoint = manager.FindEndpoint(5001, 100);
        if (registeredEndpoint.has_value())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    boost::system::error_code replaySendError;
    secondClient.send_to(
        boost::asio::buffer(validHello),
        serverEndpoint,
        0,
        replaySendError);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto endpointAfterReplay = manager.FindEndpoint(5001, 100);

    manager.Release(5001);
    serverThread.join();

    assert(!firstSendError);
    assert(!validSendError);
    assert(!replaySendError);
    assert(wrongTokenRejected);
    assert(registeredEndpoint.has_value());
    assert(registeredEndpoint->address() ==
           boost::asio::ip::address_v4::loopback());
    assert(registeredEndpoint->port() == firstClient.local_endpoint().port());
    assert(endpointAfterReplay == registeredEndpoint);
}
} // namespace

int main()
{
    TestUdpHelloRegistersEndpoint();

    std::cout << "All dungeon UDP session tests passed.\n";
    return 0;
}
