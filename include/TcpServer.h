#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <cstdint>

namespace dnf
{
class SessionManager;

class TcpServer
{
public:
    TcpServer(
        boost::asio::io_context& ioContext,
        std::uint16_t port,
        SessionManager& sessionManager);

    void Start();
    void Stop();

private:
    void StartAccept();

    std::uint16_t port_;
    boost::asio::ip::tcp::acceptor acceptor_;
    SessionManager& sessionManager_;
};
} // namespace dnf
