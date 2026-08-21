#pragma once

#include "AuthPacketDispatcher.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>

#include <cstdint>
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
        GameServerAddress gameServerAddress);

    void Start();
    void Stop();
    std::uint16_t Port() const;

private:
    void ConfigureTls(
        const std::string& certificateChainPath,
        const std::string& privateKeyPath);
    void StartAccept();

    std::uint16_t configuredPort_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::ssl::context tlsContext_;
    AccountAuthenticationService& authenticationService_;
    CharacterListService& characterListService_;
    CharacterSelectionService& characterSelectionService_;
    GameServerAddress gameServerAddress_;
};
} // namespace dnf
