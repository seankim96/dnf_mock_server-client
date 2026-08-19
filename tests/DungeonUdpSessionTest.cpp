#include "DungeonProtocol.h"
#include "DungeonUdpManager.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/system/error_code.hpp>

#include <cassert>
#include <array>
#include <chrono>
#include <iostream>
#include <thread>

namespace
{
template <typename Predicate>
bool WaitUntil(Predicate predicate)
{
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (predicate())
        {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return false;
}

void TestUdpHelloRegistersEndpoint()
{
    using boost::asio::ip::udp;

    boost::asio::io_context serverIoContext;
    dnf::DungeonUdpManager manager(serverIoContext);

    const auto port = manager.Allocate(5001, {100});
    const auto token = manager.FindToken(5001, 100);
    assert(port.has_value());
    assert(token.has_value());
    assert(!manager.AllParticipantsAuthenticated(5001));
    assert(!manager.AllParticipantsAuthenticated(9999));

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
    const bool allParticipantsAuthenticated =
        manager.AllParticipantsAuthenticated(5001);
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

    const std::vector<std::uint8_t> snapshotBytes = {1, 2, 3, 4};
    assert(manager.BroadcastSnapshot(5001, snapshotBytes));
    assert(!manager.BroadcastSnapshot(9999, snapshotBytes));
    assert(!manager.BroadcastSnapshot(
        5001,
        std::vector<std::uint8_t>(
            dnf::MAX_DUNGEON_DATAGRAM_SIZE + 1,
            0)));

    firstClient.non_blocking(true);
    std::array<std::uint8_t, dnf::MAX_DUNGEON_DATAGRAM_SIZE> receiveBuffer{};
    udp::endpoint snapshotSender;
    boost::system::error_code snapshotReceiveError;
    std::size_t snapshotSize = 0;

    for (int attempt = 0; attempt < 100; ++attempt)
    {
        snapshotSize = firstClient.receive_from(
            boost::asio::buffer(receiveBuffer),
            snapshotSender,
            0,
            snapshotReceiveError);

        if (!snapshotReceiveError)
        {
            break;
        }

        assert(snapshotReceiveError == boost::asio::error::would_block ||
               snapshotReceiveError == boost::asio::error::try_again);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const std::vector<std::uint8_t> receivedSnapshot(
        receiveBuffer.begin(),
        receiveBuffer.begin() + snapshotSize);

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
    assert(allParticipantsAuthenticated);
    assert(pendingCountAfterForeignInput == 2);
    assert(poppedFirst);
    assert(poppedSecond);
    assert(!poppedUnexpected);
    assert(receivedFirstInput.sessionId == 100);
    assert(receivedFirstInput.input.sequence == 1);
    assert(receivedSecondInput.sessionId == 100);
    assert(receivedSecondInput.input.sequence == 2);
    assert(!snapshotReceiveError);
    assert(snapshotSender.port() == port.value());
    assert(receivedSnapshot == snapshotBytes);

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    const auto heartbeat = dnf::EncodeUdpHeartbeat({5001, 100});
    firstClient.send_to(boost::asio::buffer(heartbeat), serverEndpoint);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    const auto keptActive = manager.RemoveInactiveEndpoints(
        5001,
        std::chrono::milliseconds(80));
    assert(keptActive.empty());
    assert(manager.FindEndpoint(5001, 100).has_value());

    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    const auto removedInactive = manager.RemoveInactiveEndpoints(
        5001,
        std::chrono::milliseconds(80));
    assert(removedInactive == std::vector<dnf::SessionId>({100}));
    assert(!manager.FindEndpoint(5001, 100).has_value());
    assert(manager.FindToken(5001, 100).has_value());

    firstClient.send_to(boost::asio::buffer(validHello), serverEndpoint);
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (manager.FindEndpoint(5001, 100).has_value())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(manager.FindEndpoint(5001, 100).has_value());

    manager.Release(5001);
    serverThread.join();
}

void TestAuthenticatedAttackIsQueued()
{
    using boost::asio::ip::udp;

    boost::asio::io_context serverIoContext;
    dnf::DungeonUdpManager manager(serverIoContext);

    const auto port = manager.Allocate(6001, {200});
    const auto token = manager.FindToken(6001, 200);
    assert(port.has_value());
    assert(token.has_value());

    boost::asio::io_context clientIoContext;
    udp::socket client(clientIoContext);
    client.open(udp::v4());
    client.bind(udp::endpoint(udp::v4(), 0));

    const udp::endpoint serverEndpoint(
        boost::asio::ip::address_v4::loopback(),
        port.value());

    dnf::PlayerAttackMessage firstAttack;
    firstAttack.dungeonId = 6001;
    firstAttack.sequence = 1;
    firstAttack.skillId = 1001;
    firstAttack.directionX = 1.0f;

    auto secondAttack = firstAttack;
    secondAttack.sequence = 2;

    const auto firstAttackBytes = dnf::EncodePlayerAttack(firstAttack);
    const auto secondAttackBytes = dnf::EncodePlayerAttack(secondAttack);
    const auto helloBytes = dnf::EncodeUdpHello({6001, 200, token.value()});

    std::thread serverThread(
        [&serverIoContext]
        {
            serverIoContext.run();
        });

    client.send_to(boost::asio::buffer(firstAttackBytes), serverEndpoint);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    assert(manager.PendingAttackCount(6001) == 0);

    client.send_to(boost::asio::buffer(helloBytes), serverEndpoint);
    assert(WaitUntil(
        [&manager]
        {
            return manager.FindEndpoint(6001, 200).has_value();
        }));

    client.send_to(boost::asio::buffer(firstAttackBytes), serverEndpoint);
    client.send_to(boost::asio::buffer(firstAttackBytes), serverEndpoint);
    client.send_to(boost::asio::buffer(secondAttackBytes), serverEndpoint);

    assert(WaitUntil(
        [&manager]
        {
            return manager.PendingAttackCount(6001) == 2;
        }));

    dnf::AuthenticatedPlayerAttack receivedFirst;
    dnf::AuthenticatedPlayerAttack receivedSecond;
    dnf::AuthenticatedPlayerAttack unexpected;

    assert(manager.TryPopAttack(6001, receivedFirst));
    assert(manager.TryPopAttack(6001, receivedSecond));
    assert(!manager.TryPopAttack(6001, unexpected));
    assert(!manager.TryPopAttack(9999, unexpected));
    assert(manager.PendingAttackCount(9999) == 0);

    assert(receivedFirst.sessionId == 200);
    assert(receivedFirst.attack.sequence == 1);
    assert(receivedFirst.attack.skillId == 1001);
    assert(receivedSecond.sessionId == 200);
    assert(receivedSecond.attack.sequence == 2);

    manager.Release(6001);
    serverThread.join();
}
} // namespace

int main()
{
    TestUdpHelloRegistersEndpoint();
    TestAuthenticatedAttackIsQueued();

    std::cout << "All dungeon UDP session tests passed.\n";
    return 0;
}
