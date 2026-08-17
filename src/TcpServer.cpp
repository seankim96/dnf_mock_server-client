#include "TcpServer.h"

#include "SessionManager.h"

#include <boost/system/error_code.hpp>

#include <exception>
#include <iostream>
#include <utility>

namespace dnf
{
using boost::asio::ip::tcp;

TcpServer::TcpServer(
    boost::asio::io_context& ioContext,
    std::uint16_t port,
    SessionManager& sessionManager)
    : port_(port),
      acceptor_(ioContext),
      sessionManager_(sessionManager)
{
}

void TcpServer::Start()
{
    const tcp::endpoint endpoint(tcp::v4(), port_);

    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen(boost::asio::socket_base::max_listen_connections);

    StartAccept();
}

void TcpServer::StartAccept()
{
    acceptor_.async_accept(
        [this](const boost::system::error_code& error, tcp::socket socket)
        {
            if (!error)
            {
                try
                {
                    sessionManager_.StartSession(std::move(socket));
                }
                catch (const std::exception& exception)
                {
                    std::cerr << "Failed to start session: "
                              << exception.what() << '\n';
                }
            }
            else if (error != boost::asio::error::operation_aborted)
            {
                std::cerr << "Accept error: " << error.message() << '\n';
            }

            if (acceptor_.is_open())
            {
                StartAccept();
            }
        });
}

void TcpServer::Stop()
{
    boost::system::error_code ignoredError;
    acceptor_.cancel(ignoredError);
    acceptor_.close(ignoredError);
}
} // namespace dnf
