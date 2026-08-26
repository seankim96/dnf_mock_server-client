#include "DungeonProtocol.h"
#include "DungeonUdpManager.h"
#include "DungeonMessage_generated.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/system/error_code.hpp>
#include <flatbuffers/verifier.h>

#include <cassert>
#include <array>
#include <chrono>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

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

template <typename Predicate>
bool PollOneUntil(
    boost::asio::io_context& ioContext,
    Predicate predicate)
{
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        ioContext.poll_one();
        if (predicate())
        {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return false;
}

std::vector<std::uint8_t> ReceiveDatagram(
    boost::asio::ip::udp::socket& socket,
    boost::asio::ip::udp::endpoint& sender)
{
    socket.non_blocking(true);
    std::array<std::uint8_t, dnf::MAX_DUNGEON_DATAGRAM_SIZE> buffer{};

    for (int attempt = 0; attempt < 100; ++attempt)
    {
        boost::system::error_code error;
        const std::size_t size = socket.receive_from(
            boost::asio::buffer(buffer),
            sender,
            0,
            error);

        if (!error)
        {
            return {buffer.begin(), buffer.begin() + size};
        }

        assert(error == boost::asio::error::would_block ||
               error == boost::asio::error::try_again);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    assert(false);
    return {};
}

void AssertHelloAck(
    boost::asio::ip::udp::socket& socket,
    std::uint16_t serverPort,
    dnf::DungeonId dungeonId,
    Dnf::Protocol::UdpHelloAckResult expectedResult,
    std::uint32_t expectedServerTick)
{
    boost::asio::ip::udp::endpoint sender;
    const std::vector<std::uint8_t> bytes =
        ReceiveDatagram(socket, sender);

    flatbuffers::Verifier verifier(bytes.data(), bytes.size());
    assert(Dnf::Protocol::VerifyDungeonMessageBuffer(verifier));

    const auto* message = Dnf::Protocol::GetDungeonMessage(bytes.data());
    const auto* ack = message->payload_as_UdpHelloAck();
    assert(sender.port() == serverPort);
    assert(message->dungeon_id() == dungeonId);
    assert(ack != nullptr);
    assert(ack->result() == expectedResult);
    assert(ack->server_tick() == expectedServerTick);
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
    manager.SetServerTick(37);

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
    hello.sessionId = 100;
    hello.dungeonId = 9999;
    hello.token = token.value();
    const auto wrongDungeonHello = dnf::EncodeUdpHello(hello);

    hello.dungeonId = 5001;
    hello.token = token.value() ^ 1;
    if (hello.token == 0)
    {
        hello.token = 2;
    }
    const auto wrongHello = dnf::EncodeUdpHello(hello);

    hello.token = token.value();
    const auto validHello = dnf::EncodeUdpHello(hello);

    dnf::PlayerMovementMessage movement;
    movement.dungeonId = 5001;
    movement.sequence = std::numeric_limits<std::uint32_t>::max();
    movement.moveX = 1.0f;
    const auto firstMovement = dnf::EncodePlayerMovement(movement);

    movement.sequence = 0;
    const auto secondMovement = dnf::EncodePlayerMovement(movement);

    movement.sequence = 1;
    const auto foreignEndpointMovement =
        dnf::EncodePlayerMovement(movement);

    std::thread serverThread(
        [&serverIoContext]
        {
            serverIoContext.run();
        });

    boost::system::error_code preAuthenticationMovementError;
    firstClient.send_to(
        boost::asio::buffer(firstMovement),
        serverEndpoint,
        0,
        preAuthenticationMovementError);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const bool preAuthenticationMovementRejected =
        manager.PendingMovementCount(5001) == 0;

    firstClient.send_to(
        boost::asio::buffer(wrongDungeonHello),
        serverEndpoint);
    AssertHelloAck(
        firstClient,
        port.value(),
        9999,
        Dnf::Protocol::UdpHelloAckResult_InvalidDungeon,
        0);

    boost::system::error_code firstSendError;
    firstClient.send_to(
        boost::asio::buffer(wrongHello),
        serverEndpoint,
        0,
        firstSendError);

    AssertHelloAck(
        firstClient,
        port.value(),
        5001,
        Dnf::Protocol::UdpHelloAckResult_AuthenticationFailed,
        0);

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

    AssertHelloAck(
        firstClient,
        port.value(),
        5001,
        Dnf::Protocol::UdpHelloAckResult_Success,
        37);

    boost::system::error_code firstMovementError;
    firstClient.send_to(
        boost::asio::buffer(firstMovement),
        serverEndpoint,
        0,
        firstMovementError);

    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (manager.PendingMovementCount(5001) == 1)
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    boost::system::error_code duplicateMovementError;
    firstClient.send_to(
        boost::asio::buffer(firstMovement),
        serverEndpoint,
        0,
        duplicateMovementError);

    boost::system::error_code secondMovementError;
    firstClient.send_to(
        boost::asio::buffer(secondMovement),
        serverEndpoint,
        0,
        secondMovementError);

    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (manager.PendingMovementCount(5001) == 2)
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    boost::system::error_code staleWrappedMovementError;
    firstClient.send_to(
        boost::asio::buffer(firstMovement),
        serverEndpoint,
        0,
        staleWrappedMovementError);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const std::size_t pendingCountAfterWrappedMovement =
        manager.PendingMovementCount(5001);

    boost::system::error_code replaySendError;
    secondClient.send_to(
        boost::asio::buffer(validHello),
        serverEndpoint,
        0,
        replaySendError);

    AssertHelloAck(
        secondClient,
        port.value(),
        5001,
        Dnf::Protocol::UdpHelloAckResult_AuthenticationFailed,
        0);

    boost::system::error_code foreignMovementError;
    secondClient.send_to(
        boost::asio::buffer(foreignEndpointMovement),
        serverEndpoint,
        0,
        foreignMovementError);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto endpointAfterReplay = manager.FindEndpoint(5001, 100);
    const bool allParticipantsAuthenticated =
        manager.AllParticipantsAuthenticated(5001);
    const std::size_t pendingCountAfterForeignInput =
        manager.PendingMovementCount(5001);

    dnf::AuthenticatedPlayerMovement receivedFirstMovement;
    dnf::AuthenticatedPlayerMovement receivedSecondMovement;
    dnf::AuthenticatedPlayerMovement unexpectedMovement;
    const bool poppedFirst =
        manager.TryPopMovement(5001, receivedFirstMovement);
    const bool poppedSecond =
        manager.TryPopMovement(5001, receivedSecondMovement);
    const bool poppedUnexpected =
        manager.TryPopMovement(5001, unexpectedMovement);

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

    assert(WaitUntil(
        [&manager]
        {
            const auto stats = manager.FindStats(5001);
            return stats.has_value() &&
                   stats->sentSnapshotDatagramCount == 1;
        }));

    const auto snapshotStats = manager.FindStats(5001);

    assert(!preAuthenticationMovementError);
    assert(!firstSendError);
    assert(!validSendError);
    assert(!firstMovementError);
    assert(!duplicateMovementError);
    assert(!secondMovementError);
    assert(!staleWrappedMovementError);
    assert(!replaySendError);
    assert(!foreignMovementError);
    assert(preAuthenticationMovementRejected);
    assert(wrongTokenRejected);
    assert(registeredEndpoint.has_value());
    assert(registeredEndpoint->address() ==
           boost::asio::ip::address_v4::loopback());
    assert(registeredEndpoint->port() == firstClient.local_endpoint().port());
    assert(endpointAfterReplay == registeredEndpoint);
    assert(allParticipantsAuthenticated);
    assert(pendingCountAfterForeignInput == 2);
    assert(pendingCountAfterWrappedMovement == 2);
    assert(poppedFirst);
    assert(poppedSecond);
    assert(!poppedUnexpected);
    assert(receivedFirstMovement.sessionId == 100);
    assert(receivedFirstMovement.movement.sequence ==
           std::numeric_limits<std::uint32_t>::max());
    assert(receivedSecondMovement.sessionId == 100);
    assert(receivedSecondMovement.movement.sequence == 0);
    assert(!snapshotReceiveError);
    assert(snapshotSender.port() == port.value());
    assert(receivedSnapshot == snapshotBytes);
    assert(snapshotStats.has_value());
    assert(snapshotStats->acceptedSnapshotCount == 1);
    assert(snapshotStats->sentSnapshotDatagramCount == 1);
    assert(snapshotStats->oversizedSnapshotCount == 1);

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
    AssertHelloAck(
        firstClient,
        port.value(),
        5001,
        Dnf::Protocol::UdpHelloAckResult_Success,
        37);

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
    firstAttack.sequence = std::numeric_limits<std::uint32_t>::max();
    firstAttack.skillId = 1001;
    firstAttack.directionX = 1.0f;

    auto secondAttack = firstAttack;
    secondAttack.sequence = 0;

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

    client.send_to(boost::asio::buffer(firstAttackBytes), serverEndpoint);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    assert(manager.PendingAttackCount(6001) == 2);

    dnf::AuthenticatedPlayerAttack receivedFirst;
    dnf::AuthenticatedPlayerAttack receivedSecond;
    dnf::AuthenticatedPlayerAttack unexpected;

    assert(manager.TryPopAttack(6001, receivedFirst));
    assert(manager.TryPopAttack(6001, receivedSecond));
    assert(!manager.TryPopAttack(6001, unexpected));
    assert(!manager.TryPopAttack(9999, unexpected));
    assert(manager.PendingAttackCount(9999) == 0);

    assert(receivedFirst.sessionId == 200);
    assert(receivedFirst.attack.sequence ==
           std::numeric_limits<std::uint32_t>::max());
    assert(receivedFirst.attack.skillId == 1001);
    assert(receivedSecond.sessionId == 200);
    assert(receivedSecond.attack.sequence == 0);

    manager.Release(6001);
    serverThread.join();
}

void TestLatestSnapshotReplacesPendingSnapshot()
{
    using boost::asio::ip::udp;

    boost::asio::io_context serverIoContext;
    dnf::DungeonUdpManager manager(serverIoContext);

    const auto port = manager.Allocate(7001, {300});
    const auto token = manager.FindToken(7001, 300);
    assert(port.has_value());
    assert(token.has_value());

    boost::asio::io_context clientIoContext;
    udp::socket client(clientIoContext, udp::endpoint(udp::v4(), 0));
    const udp::endpoint serverEndpoint(
        boost::asio::ip::address_v4::loopback(),
        port.value());

    serverIoContext.poll();

    const auto hello = dnf::EncodeUdpHello(
        {7001, 300, token.value()});
    client.send_to(boost::asio::buffer(hello), serverEndpoint);

    assert(PollOneUntil(
        serverIoContext,
        [&manager]
        {
            return manager.FindEndpoint(7001, 300).has_value();
        }));
    AssertHelloAck(
        client,
        port.value(),
        7001,
        Dnf::Protocol::UdpHelloAckResult_Success,
        0);

    const std::vector<std::uint8_t> firstSnapshot = {1};
    const std::vector<std::uint8_t> replacedSnapshot = {2};
    const std::vector<std::uint8_t> latestSnapshot = {3};

    assert(manager.BroadcastSnapshot(7001, firstSnapshot));
    assert(PollOneUntil(
        serverIoContext,
        [&manager]
        {
            const auto stats = manager.FindStats(7001);
            return stats.has_value() && stats->snapshotSendInProgress;
        }));

    assert(manager.BroadcastSnapshot(7001, replacedSnapshot));
    assert(manager.BroadcastSnapshot(7001, latestSnapshot));

    const auto pendingStats = manager.FindStats(7001);
    assert(pendingStats.has_value());
    assert(pendingStats->acceptedSnapshotCount == 3);
    assert(pendingStats->replacedSnapshotCount == 1);
    assert(pendingStats->snapshotPending);
    assert(pendingStats->snapshotSendInProgress);

    assert(PollOneUntil(
        serverIoContext,
        [&manager]
        {
            const auto stats = manager.FindStats(7001);
            return stats.has_value() &&
                   stats->sentSnapshotDatagramCount == 2 &&
                   !stats->snapshotPending &&
                   !stats->snapshotSendInProgress;
        }));

    udp::endpoint sender;
    assert(ReceiveDatagram(client, sender) == firstSnapshot);
    assert(ReceiveDatagram(client, sender) == latestSnapshot);

    assert(manager.Release(7001));
    serverIoContext.poll();
}
} // namespace

int main()
{
    TestUdpHelloRegistersEndpoint();
    TestAuthenticatedAttackIsQueued();
    TestLatestSnapshotReplacesPendingSnapshot();

    std::cout << "All dungeon UDP session tests passed.\n";
    return 0;
}
