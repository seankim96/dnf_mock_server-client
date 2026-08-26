#include "AuthTlsServer.h"

#include "AuthTlsSession.h"

#include <openssl/ssl.h>

#include <boost/asio/dispatch.hpp>
#include <boost/system/error_code.hpp>

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace dnf
{
using boost::asio::ip::tcp;

AuthTlsServer::AuthTlsServer(
    boost::asio::io_context& ioContext,
    std::uint16_t port,
    const std::string& certificateChainPath,
    const std::string& privateKeyPath,
    AccountAuthenticationService& authenticationService,
    CharacterListService& characterListService,
    CharacterSelectionService& characterSelectionService,
    GameServerAddress gameServerAddress,
    NetworkSessionOptions sessionOptions)
    : configuredPort_(port),
      strand_(boost::asio::make_strand(ioContext)),
      acceptor_(strand_),
      tlsContext_(boost::asio::ssl::context::tls_server),
      authenticationService_(authenticationService),
      characterListService_(characterListService),
      characterSelectionService_(characterSelectionService),
      gameServerAddress_(std::move(gameServerAddress)),
      sessionOptions_(std::move(sessionOptions))
{
    if (gameServerAddress_.host.empty() || gameServerAddress_.port == 0)
    {
        throw std::invalid_argument("Game server address is invalid");
    }

    if (!sessionOptions_.IsValid())
    {
        throw std::invalid_argument(
            "Auth TLS session options are invalid");
    }

    ConfigureTls(certificateChainPath, privateKeyPath);
}

void AuthTlsServer::ConfigureTls(
    const std::string& certificateChainPath,
    const std::string& privateKeyPath)
{
    if (certificateChainPath.empty() || privateKeyPath.empty())
    {
        throw std::invalid_argument(
            "TLS certificate and private key paths are required");
    }

    tlsContext_.set_options(
        boost::asio::ssl::context::default_workarounds |
        boost::asio::ssl::context::no_sslv2 |
        boost::asio::ssl::context::no_sslv3 |
        boost::asio::ssl::context::single_dh_use);

    if (SSL_CTX_set_min_proto_version(
            tlsContext_.native_handle(), TLS1_2_VERSION) != 1)
    {
        throw std::runtime_error(
            "Failed to set the minimum TLS version");
    }

    tlsContext_.use_certificate_chain_file(certificateChainPath);
    tlsContext_.use_private_key_file(
        privateKeyPath,
        boost::asio::ssl::context::pem);

    if (SSL_CTX_check_private_key(tlsContext_.native_handle()) != 1)
    {
        throw std::runtime_error(
            "TLS certificate and private key do not match");
    }
}

void AuthTlsServer::Start()
{
    if (stopped_.load())
    {
        throw std::runtime_error("Auth TLS server has been stopped");
    }

    if (acceptor_.is_open())
    {
        throw std::runtime_error("Auth TLS server is already started");
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
        StartAccept();
    }
    catch (...)
    {
        stopped_.store(true);
        CloseAcceptor();
        throw;
    }
}

void AuthTlsServer::StartAccept()
{
    acceptor_.async_accept(
        [this](const boost::system::error_code& error, tcp::socket socket)
        {
            if (!error)
            {
                try
                {
                    std::make_shared<AuthTlsSession>(
                        std::move(socket),
                        tlsContext_,
                        authenticationService_,
                        characterListService_,
                        characterSelectionService_,
                        gameServerAddress_,
                        sessionOptions_)
                        ->Start();
                }
                catch (const std::exception& exception)
                {
                    std::cerr << "Failed to start auth TLS session: "
                              << exception.what() << '\n';
                }
            }
            else if (error != boost::asio::error::operation_aborted)
            {
                std::cerr << "Auth TLS accept error: "
                          << error.message() << '\n';
            }

            if (!stopped_.load() && acceptor_.is_open())
            {
                StartAccept();
            }
        });
}

void AuthTlsServer::Stop(std::function<void()> onStopped)
{
    stopped_.store(true);

    boost::asio::dispatch(
        strand_,
        [this, onStopped = std::move(onStopped)]() mutable
        {
            CloseAcceptor();
            if (onStopped)
            {
                onStopped();
            }
        });
}

void AuthTlsServer::CloseAcceptor()
{
    boost::system::error_code ignoredError;
    acceptor_.cancel(ignoredError);
    acceptor_.close(ignoredError);
}

std::uint16_t AuthTlsServer::Port() const
{
    const std::uint16_t boundPort = boundPort_.load();
    return boundPort == 0 ? configuredPort_ : boundPort;
}
} // namespace dnf
