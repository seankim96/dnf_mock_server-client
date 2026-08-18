#pragma once

#include "Packet.h"
#include "PacketDispatcher.h"
#include "ReceiveBuffer.h"
#include "SessionId.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace dnf
{
class ChannelManager;
class SessionManager;

class TcpSession : public std::enable_shared_from_this<TcpSession>
{
public:
    TcpSession(
        SessionId sessionId,
        boost::asio::ip::tcp::socket socket,
        SessionManager& sessionManager,
        ChannelManager& channelManager);

    void Start();
    SessionId Id() const;

private:
    void StartRead();
    void HandleRead(std::size_t receivedSize);
    void QueueWrite(std::vector<std::uint8_t> data);
    void StartWrite();
    void Close();

    SessionId sessionId_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    SessionManager& sessionManager_;

    std::array<std::uint8_t, 4096> receivedBytes_{};
    ReceiveBuffer receiveBuffer_;
    PacketDispatcher dispatcher_;
    std::deque<std::vector<std::uint8_t>> writeQueue_;
    bool closed_ = false;
};
} // namespace dnf
