#pragma once

#include "AccountAuthenticationService.h"
#include "AuthTicketIssuer.h"
#include "AuthTlsServer.h"
#include "CharacterListService.h"
#include "CharacterSelectionService.h"
#include "DatabaseExecutor.h"
#include "ScryptPasswordHasher.h"
#include "SqliteAccountPlayerRepository.h"
#include "SqliteAccountRepository.h"
#include "SqliteAuthTicketStore.h"
#include "SqliteDatabase.h"
#include "SqlitePlayerRepository.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <cstdint>
#include <string>

namespace dnf
{
class AuthServerApplication
{
public:
    AuthServerApplication(
        std::uint16_t port,
        const std::string& databasePath,
        const std::string& certificateChainPath,
        const std::string& privateKeyPath,
        GameServerAddress gameServerAddress);

    void Start();
    void Run();
    void Stop();

    std::uint16_t Port() const;

private:
    void WaitForShutdownSignal();
    void StopOnIoContext();

    boost::asio::io_context ioContext_;
    boost::asio::signal_set shutdownSignals_;
    SqliteDatabase database_;
    SqliteAccountRepository accountRepository_;
    SqlitePlayerRepository playerRepository_;
    SqliteAccountPlayerRepository accountPlayerRepository_;
    SqliteAuthTicketStore authTicketStore_;
    AuthTicketIssuer authTicketIssuer_;
    ScryptPasswordHasher passwordHasher_;
    DatabaseExecutor databaseExecutor_;
    AccountAuthenticationService authenticationService_;
    CharacterListService characterListService_;
    CharacterSelectionService characterSelectionService_;
    AuthTlsServer tlsServer_;
};
} // namespace dnf
