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

    dnf::PlayerInputMessage input;
    input.dungeonId = 5001;
    input.sequence = 1;
    input.moveX = 1.0f;
    const auto firstInput = dnf::EncodePlayerInput(input);

    input.sequence = 2;
    const auto secondInput = dnf::EncodePlayerInput(input);

    input.sequence = 3;
    const auto foreignEndpointInput = dnf::EncodePlayerInput(input);

    std::thread serverThread(
        [&serverIoContext]
        {
            serverIoContext.run();
        });

    boost::system::error_code preAuthenticationInputError;
    firstClient.send_to(
        boost::asio::buffer(firstInput),
        serverEndpoint,
        0,
        preAuthenticationInputError);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const bool preAuthenticationInputRejected =
        manager.PendingInputCount(5001) == 0;

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

    boost::system::error_code firstInputError;
    firstClient.send_to(
        boost::asio::buffer(firstInput),
        serverEndpoint,
        0,
        firstInputError);

    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (manager.PendingInputCount(5001) == 1)
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    boost::system::error_code duplicateInputError;
    firstClient.send_to(
        boost::asio::buffer(firstInput),
        serverEndpoint,
        0,
        duplicateInputError);

    boost::system::error_code secondInputError;
    firstClient.send_to(
        boost::asio::buffer(secondInput),
        serverEndpoint,
        0,
        secondInputError);

    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (manager.PendingInputCount(5001) == 2)
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

    boost::system::error_code foreignInputError;
    secondClient.send_to(
        boost::asio::buffer(foreignEndpointInput),
        serverEndpoint,
        0,
        foreignInputError);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto endpointAfterReplay = manager.FindEndpoint(5001, 100);
    const std::size_t pendingCountAfterForeignInput =
        manager.PendingInputCount(5001);

    dnf::AuthenticatedPlayerInput receivedFirstInput;
    dnf::AuthenticatedPlayerInput receivedSecondInput;
    dnf::AuthenticatedPlayerInput unexpectedInput;
    const bool poppedFirst =
        manager.TryPopInput(5001, receivedFirstInput);
    const bool poppedSecond =
        manager.TryPopInput(5001, receivedSecondInput);
    const bool poppedUnexpected =
        manager.TryPopInput(5001, unexpectedInput);

    manager.Release(5001);
    serverThread.join();

    assert(!preAuthenticationInputError);
    assert(!firstSendError);
    assert(!validSendError);
    assert(!firstInputError);
    assert(!duplicateInputError);
    assert(!secondInputError);
    assert(!replaySendError);
    assert(!foreignInputError);
    assert(preAuthenticationInputRejected);
    assert(wrongTokenRejected);
    assert(registeredEndpoint.has_value());
    assert(registeredEndpoint->address() ==
           boost::asio::ip::address_v4::loopback());
    assert(registeredEndpoint->port() == firstClient.local_endpoint().port());
    assert(endpointAfterReplay == registeredEndpoint);
    assert(pendingCountAfterForeignInput == 2);
    assert(poppedFirst);
    assert(poppedSecond);
    assert(!poppedUnexpected);
    assert(receivedFirstInput.sessionId == 100);
    assert(receivedFirstInput.input.sequence == 1);
    assert(receivedSecondInput.sessionId == 100);
    assert(receivedSecondInput.input.sequence == 2);
}
} // namespace

int main()
{
    TestUdpHelloRegistersEndpoint();

    std::cout << "All dungeon UDP session tests passed.\n";
    return 0;
}
