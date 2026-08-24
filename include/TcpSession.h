#pragma once

#include "NetworkSessionOptions.h"
#include "Packet.h"
#include "PacketDispatcher.h"
#include "ReceiveBuffer.h"
#include "SessionDeadline.h"
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
class DungeonManager;
class DungeonUdpManager;
class PartyManager;
class PlayerLoginService;
class SessionManager;

class TcpSession : public std::enable_shared_from_this<TcpSession>
{
public:
    TcpSession(
        SessionId sessionId,
        boost::asio::ip::tcp::socket socket,
        SessionManager& sessionManager,
        ChannelManager& channelManager,
        PartyManager& partyManager,
        DungeonManager& dungeonManager,
        DungeonUdpManager& dungeonUdpManager,
        PlayerLoginService& playerLoginService,
        NetworkSessionOptions options = {});

    void Start();
    SessionId Id() const;

private:
    void StartRead();
    void HandleRead(std::size_t receivedSize);
    void DispatchNextPacket();
    void HandleResponse(std::vector<std::uint8_t> response);
    bool QueueWrite(std::vector<std::uint8_t> data);
    void StartWrite();
    void StartAuthenticationTimeout();
    void StartReadTimeout();
    void StartWriteTimeout();
    void Close();

    SessionId sessionId_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    SessionManager& sessionManager_;
    NetworkSessionOptions options_;
    SessionDeadline authenticationDeadline_;
    SessionDeadline readDeadline_;
    SessionDeadline writeDeadline_;

    std::array<std::uint8_t, 4096> receivedBytes_{};
    ReceiveBuffer receiveBuffer_;
    PacketDispatcher dispatcher_;
    std::deque<std::vector<std::uint8_t>> writeQueue_;
    std::size_t pendingWriteBytes_ = 0;
    bool requestInProgress_ = false;
    bool closed_ = false;
};
} // namespace dnf
