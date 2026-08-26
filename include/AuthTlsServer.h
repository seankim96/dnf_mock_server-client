#pragma once

#include "AuthPacketDispatcher.h"
#include "NetworkSessionOptions.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace dnf
{
class AccountAuthenticationService;
class CharacterListService;
class CharacterSelectionService;

class AuthTlsServer
{
public:
    AuthTlsServer(
        boost::asio::io_context& ioContext,
        std::uint16_t port,
        const std::string& certificateChainPath,
        const std::string& privateKeyPath,
        AccountAuthenticationService& authenticationService,
        CharacterListService& characterListService,
        CharacterSelectionService& characterSelectionService,
        GameServerAddress gameServerAddress,
        NetworkSessionOptions sessionOptions = {});

    void Start();
    void Stop(std::function<void()> onStopped = {});
    std::uint16_t Port() const;

private:
    void ConfigureTls(
        const std::string& certificateChainPath,
        const std::string& privateKeyPath);
    void StartAccept();
    void CloseAcceptor();

    std::uint16_t configuredPort_;
    std::atomic<std::uint16_t> boundPort_{0};
    std::atomic<bool> stopped_{false};
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::ssl::context tlsContext_;
    AccountAuthenticationService& authenticationService_;
    CharacterListService& characterListService_;
    CharacterSelectionService& characterSelectionService_;
    GameServerAddress gameServerAddress_;
    NetworkSessionOptions sessionOptions_;
};
} // namespace dnf
