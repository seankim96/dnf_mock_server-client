#pragma once

#include "DungeonInstance.h"
#include "DungeonProtocol.h"
#include "DungeonUdpTypes.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/error_code.hpp>

#include <array>
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
constexpr std::size_t MAX_PENDING_DUNGEON_INPUTS = 256;
constexpr std::size_t MAX_PENDING_DUNGEON_ATTACKS = 256;

struct AuthenticatedPlayerInput
{
    SessionId sessionId = 0;
    PlayerInputMessage input;
};

struct AuthenticatedPlayerAttack
{
    SessionId sessionId = 0;
    PlayerAttackMessage attack;
};

class DungeonUdpSession
    : public std::enable_shared_from_this<DungeonUdpSession>
{
public:
    using TokenMap = std::unordered_map<SessionId, DungeonUdpToken>;

    DungeonUdpSession(
        DungeonId dungeonId,
        boost::asio::ip::udp::socket socket,
        TokenMap tokens);

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
    bool TryPopInput(AuthenticatedPlayerInput& output);
    std::size_t PendingInputCount() const;
    bool TryPopAttack(AuthenticatedPlayerAttack& output);
    std::size_t PendingAttackCount() const;

private:
    void StartReceive();
    void HandleReceive(
        const boost::system::error_code& error,
        std::size_t receivedSize);
    void HandleHello(const UdpHelloMessage& hello);
    void HandleHeartbeat(const UdpHeartbeatMessage& heartbeat);
    void HandlePlayerInput(const PlayerInputMessage& input);
    void HandlePlayerAttack(const PlayerAttackMessage& attack);
    void SendSnapshotOnStrand(
        std::shared_ptr<const std::vector<std::uint8_t>> bytes);

    DungeonId dungeonId_;
    boost::asio::ip::udp::socket socket_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    std::uint16_t port_;

    std::array<std::uint8_t, MAX_DUNGEON_DATAGRAM_SIZE> receiveBuffer_{};
    boost::asio::ip::udp::endpoint senderEndpoint_;
    bool stopped_ = false;

    mutable std::mutex stateMutex_;
    TokenMap tokens_;
    std::unordered_map<SessionId, boost::asio::ip::udp::endpoint> endpoints_;
    std::unordered_map<
        SessionId,
        std::chrono::steady_clock::time_point> lastActivity_;
    std::unordered_map<SessionId, std::uint32_t> lastSequences_;
    std::unordered_map<SessionId, std::uint32_t> lastAttackSequences_;
    std::deque<AuthenticatedPlayerInput> pendingInputs_;
    std::deque<AuthenticatedPlayerAttack> pendingAttacks_;
};
} // namespace dnf
