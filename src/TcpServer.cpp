#include "TcpServer.h"

#include "SessionManager.h"

#include <boost/asio/dispatch.hpp>
#include <boost/system/error_code.hpp>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace dnf
{
using boost::asio::ip::tcp;

TcpServer::TcpServer(
    boost::asio::io_context& ioContext,
    std::uint16_t port,
    SessionManager& sessionManager)
    : configuredPort_(port),
      strand_(boost::asio::make_strand(ioContext)),
      acceptor_(strand_),
      sessionManager_(sessionManager)
{
}

void TcpServer::Start()
{
    if (stopped_.load())
    {
        throw std::runtime_error("TCP server has been stopped");
    }

    if (acceptor_.is_open())
    {
        throw std::runtime_error("TCP server is already started");
    }

    const tcp::endpoint endpoint(tcp::v4(), configuredPort_);

    try
    {
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen(
            boost::asio::socket_base::max_listen_connections);
        boundPort_.store(acceptor_.local_endpoint().port());
    }
    catch (...)
    {
        Stop();
        throw;
    }

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
    if (stopped_.exchange(true))
    {
        return;
    }

    boost::asio::dispatch(
        strand_,
        [this]
        {
            boost::system::error_code ignoredError;
            acceptor_.cancel(ignoredError);
            acceptor_.close(ignoredError);
        });
}

std::uint16_t TcpServer::Port() const
{
    const std::uint16_t boundPort = boundPort_.load();
    return boundPort == 0 ? configuredPort_ : boundPort;
}
} // namespace dnf
