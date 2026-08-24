#pragma once

#include "DungeonInstance.h"
#include "DungeonProtocol.h"
#include "DungeonUdpTypes.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace dnf
{
constexpr std::size_t MAX_DUNGEON_DATAGRAM_SIZE = 1200;
constexpr std::size_t MAX_PENDING_DUNGEON_MOVEMENTS = 256;
constexpr std::size_t MAX_PENDING_DUNGEON_ATTACKS = 256;

struct AuthenticatedPlayerMovement
{
    SessionId sessionId = 0;
    PlayerMovementMessage movement;
};

struct AuthenticatedPlayerAttack
{
    SessionId sessionId = 0;
    PlayerAttackMessage attack;
};

struct DungeonUdpSessionStats
{
    std::uint64_t acceptedSnapshotCount = 0;
    std::uint64_t replacedSnapshotCount = 0;
    std::uint64_t sentSnapshotDatagramCount = 0;
    std::uint64_t snapshotSendErrorCount = 0;
    std::uint64_t oversizedSnapshotCount = 0;
    bool snapshotPending = false;
    bool snapshotSendInProgress = false;
};

class DungeonUdpSession
    : public std::enable_shared_from_this<DungeonUdpSession>
{
public:
    using TokenMap = std::unordered_map<SessionId, DungeonUdpToken>;

    DungeonUdpSession(
        DungeonId dungeonId,
        boost::asio::ip::udp::socket socket,
        TokenMap tokens,
        std::shared_ptr<const std::atomic<std::uint32_t>> serverTick);

    void Start();
    void Stop();

    std::uint16_t Port() const;
    std::optional<DungeonUdpToken> FindToken(SessionId sessionId) const;
    std::optional<boost::asio::ip::udp::endpoint> FindEndpoint(
        SessionId sessionId) const;
    bool AllParticipantsAuthenticated() const;
    void RefreshAllActivity();
    std::vector<SessionId> RemoveInactiveEndpoints(
        std::chrono::milliseconds idleTimeout);
    bool SendSnapshot(std::vector<std::uint8_t> bytes);
    bool TryPopMovement(AuthenticatedPlayerMovement& output);
    std::size_t PendingMovementCount() const;
    bool TryPopAttack(AuthenticatedPlayerAttack& output);
    std::size_t PendingAttackCount() const;
    DungeonUdpSessionStats Stats() const;

private:
    void StartReceive();
    void HandleReceive(
        const boost::system::error_code& error,
        std::size_t receivedSize);
    void HandleHello(const UdpHelloMessage& hello);
    void SendHelloAck(
        DungeonId requestedDungeonId,
        UdpHelloResult result);
    void HandleHeartbeat(const UdpHeartbeatMessage& heartbeat);
    void HandlePlayerMovement(const PlayerMovementMessage& movement);
    void HandlePlayerAttack(const PlayerAttackMessage& attack);
    void PumpSnapshotOnStrand();
    void SendSnapshotToNextEndpoint();
    void FinishSnapshotOnStrand();

    DungeonId dungeonId_;
    std::shared_ptr<const std::atomic<std::uint32_t>> serverTick_;
    boost::asio::ip::udp::socket socket_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    std::uint16_t port_;

    std::array<std::uint8_t, MAX_DUNGEON_DATAGRAM_SIZE> receiveBuffer_{};
    boost::asio::ip::udp::endpoint senderEndpoint_;
    std::atomic<bool> stopped_{false};

    mutable std::mutex stateMutex_;
    TokenMap tokens_;
    std::unordered_map<SessionId, boost::asio::ip::udp::endpoint> endpoints_;
    std::unordered_map<
        SessionId,
        std::chrono::steady_clock::time_point> lastActivity_;
    std::unordered_map<SessionId, std::uint32_t> lastMovementSequences_;
    std::unordered_map<SessionId, std::uint32_t> lastAttackSequences_;
    std::deque<AuthenticatedPlayerMovement> pendingMovements_;
    std::deque<AuthenticatedPlayerAttack> pendingAttacks_;

    mutable std::mutex snapshotMutex_;
    DungeonUdpSessionStats snapshotStats_;
    std::shared_ptr<const std::vector<std::uint8_t>> pendingSnapshot_;
    bool snapshotPumpScheduled_ = false;

    std::shared_ptr<const std::vector<std::uint8_t>> activeSnapshot_;
    std::vector<boost::asio::ip::udp::endpoint> snapshotDestinations_;
    std::size_t nextSnapshotDestination_ = 0;
};
} // namespace dnf
