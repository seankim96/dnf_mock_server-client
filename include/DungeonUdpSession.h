#pragma once

#include "DungeonInstance.h"
#include "DungeonUdpTypes.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace dnf
{
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

private:
    void StartReceive();
    void HandleReceive(
        const boost::system::error_code& error,
        std::size_t receivedSize);

    DungeonId dungeonId_;
    boost::asio::ip::udp::socket socket_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    std::uint16_t port_;

    std::array<std::uint8_t, 1200> receiveBuffer_{};
    boost::asio::ip::udp::endpoint senderEndpoint_;
    bool stopped_ = false;

    mutable std::mutex authenticationMutex_;
    TokenMap tokens_;
    std::unordered_map<SessionId, boost::asio::ip::udp::endpoint> endpoints_;
};
} // namespace dnf
