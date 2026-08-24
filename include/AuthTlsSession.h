#pragma once

#include "AuthPacketDispatcher.h"
#include "NetworkSessionOptions.h"
#include "ReceiveBuffer.h"
#include "SessionDeadline.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/strand.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace dnf
{
class AccountAuthenticationService;
class CharacterListService;
class CharacterSelectionService;

class AuthTlsSession
    : public std::enable_shared_from_this<AuthTlsSession>
{
public:
    AuthTlsSession(
        boost::asio::ip::tcp::socket socket,
        boost::asio::ssl::context& tlsContext,
        AccountAuthenticationService& authenticationService,
        CharacterListService& characterListService,
        CharacterSelectionService& characterSelectionService,
        GameServerAddress gameServerAddress,
        NetworkSessionOptions options = {});

    void Start();

private:
    void StartHandshake();
    void StartRead();
    void HandleRead(std::size_t receivedSize);
    void DispatchNextPacket();
    void HandleResponse(std::vector<std::uint8_t> response);
    bool QueueWrite(std::vector<std::uint8_t> data);
    void StartWrite();
    void StartHandshakeTimeout();
    void StartAuthenticationTimeout();
    void StartReadTimeout();
    void StartWriteTimeout();
    void Close();

    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> stream_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    NetworkSessionOptions options_;
    SessionDeadline handshakeDeadline_;
    SessionDeadline authenticationDeadline_;
    SessionDeadline readDeadline_;
    SessionDeadline writeDeadline_;
    std::array<std::uint8_t, 4096> receivedBytes_{};
    ReceiveBuffer receiveBuffer_;
    AuthPacketDispatcher dispatcher_;
    std::deque<std::vector<std::uint8_t>> writeQueue_;
    std::size_t pendingWriteBytes_ = 0;
    bool requestInProgress_ = false;
    bool closed_ = false;
};
} // namespace dnf
