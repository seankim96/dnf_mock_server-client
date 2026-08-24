#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>

#include <atomic>
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
    std::uint16_t Port() const;

private:
    void StartAccept();

    std::uint16_t configuredPort_;
    std::atomic<std::uint16_t> boundPort_{0};
    std::atomic<bool> stopped_{false};
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::ip::tcp::acceptor acceptor_;
    SessionManager& sessionManager_;
};
} // namespace dnf
