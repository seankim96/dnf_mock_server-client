#pragma once

#include "DungeonInstance.h"
#include "DungeonProtocol.h"
#include "DungeonUdpTypes.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace dnf
{
constexpr std::size_t MAX_PENDING_DUNGEON_INPUTS = 256;

struct AuthenticatedPlayerInput
{
    SessionId sessionId = 0;
    PlayerInputMessage input;
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
    bool TryPopInput(AuthenticatedPlayerInput& output);
    std::size_t PendingInputCount() const;

private:
    void StartReceive();
    void HandleReceive(
        const boost::system::error_code& error,
        std::size_t receivedSize);
    void HandleHello(const UdpHelloMessage& hello);
    void HandlePlayerInput(const PlayerInputMessage& input);

    DungeonId dungeonId_;
    boost::asio::ip::udp::socket socket_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    std::uint16_t port_;

    std::array<std::uint8_t, 1200> receiveBuffer_{};
    boost::asio::ip::udp::endpoint senderEndpoint_;
    bool stopped_ = false;

    mutable std::mutex stateMutex_;
    TokenMap tokens_;
    std::unordered_map<SessionId, boost::asio::ip::udp::endpoint> endpoints_;
    std::unordered_map<SessionId, std::uint32_t> lastSequences_;
    std::deque<AuthenticatedPlayerInput> pendingInputs_;
};
} // namespace dnf
