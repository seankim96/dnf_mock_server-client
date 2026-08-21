#include "AccountAuthenticationService.h"
#include "AuthTicketIssuer.h"
#include "AuthTlsServer.h"
#include "CharacterListService.h"
#include "CharacterSelectionService.h"
#include "DatabaseExecutor.h"
#include "PasswordHasher.h"
#include "SqliteAccountPlayerRepository.h"
#include "SqliteAccountRepository.h"
#include "SqliteAuthTicketStore.h"
#include "SqliteDatabase.h"
#include "SqlitePlayerRepository.h"
#include "TestTlsCertificate.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/system/error_code.hpp>

#include <cassert>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

using boost::asio::ip::tcp;

namespace
{
class TestPasswordHasher final : public dnf::PasswordHasher
{
public:
    std::optional<std::string> Hash(
        const std::string& password) override
    {
        return "test-hash:" + password;
    }

    bool Verify(
        const std::string& password,
        const std::string& encodedPasswordHash) override
    {
        return encodedPasswordHash == "test-hash:" + password;
    }
};

struct TestServices
{
    TestServices()
        : database(":memory:"),
          accountRepository(database),
          playerRepository(database),
          accountPlayerRepository(database),
          ticketStore(database),
          ticketIssuer(accountPlayerRepository, ticketStore),
          databaseExecutor(1),
          authenticationService(
              ioContext,
              databaseExecutor,
              accountRepository,
              passwordHasher),
          characterListService(
              ioContext,
              databaseExecutor,
              accountRepository,
              accountPlayerRepository,
              playerRepository),
          characterSelectionService(
              ioContext,
              databaseExecutor,
              ticketIssuer)
    {
    }

    boost::asio::io_context ioContext;
    dnf::SqliteDatabase database;
    dnf::SqliteAccountRepository accountRepository;
    dnf::SqlitePlayerRepository playerRepository;
    dnf::SqliteAccountPlayerRepository accountPlayerRepository;
    dnf::SqliteAuthTicketStore ticketStore;
    dnf::AuthTicketIssuer ticketIssuer;
    TestPasswordHasher passwordHasher;
    dnf::DatabaseExecutor databaseExecutor;
    dnf::AccountAuthenticationService authenticationService;
    dnf::CharacterListService characterListService;
    dnf::CharacterSelectionService characterSelectionService;
};

void TestTlsConfigurationIsRequired()
{
    TestServices services;
    bool rejected = false;

    try
    {
        dnf::AuthTlsServer server(
            services.ioContext,
            0,
            "",
            "",
            services.authenticationService,
            services.characterListService,
            services.characterSelectionService,
            {"127.0.0.1", 7777});
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }

    assert(rejected);
}

void TestServerAcceptsTlsConnection()
{
    TestServices services;
    dnf::test::TestTlsCertificateFiles certificateFiles;
    dnf::AuthTlsServer server(
        services.ioContext,
        0,
        certificateFiles.CertificatePath(),
        certificateFiles.PrivateKeyPath(),
        services.authenticationService,
        services.characterListService,
        services.characterSelectionService,
        {"127.0.0.1", 7777});

    server.Start();
    const std::uint16_t port = server.Port();
    assert(port != 0);

    bool duplicateStartRejected = false;
    try
    {
        server.Start();
    }
    catch (const std::runtime_error&)
    {
        duplicateStartRejected = true;
    }
    assert(duplicateStartRejected);

    std::exception_ptr serverException;
    std::thread serverThread(
        [&]
        {
            try
            {
                services.ioContext.run();
            }
            catch (...)
            {
                serverException = std::current_exception();
            }
        });

    try
    {
        boost::asio::io_context clientIoContext;
        boost::asio::ssl::context clientTlsContext(
            boost::asio::ssl::context::tls_client);
        clientTlsContext.set_verify_mode(
            boost::asio::ssl::verify_none);
        boost::asio::ssl::stream<tcp::socket> client(
            clientIoContext,
            clientTlsContext);

        client.next_layer().connect(tcp::endpoint(
            boost::asio::ip::address_v4::loopback(),
            port));
        client.handshake(boost::asio::ssl::stream_base::client);

        boost::system::error_code ignoredError;
        client.shutdown(ignoredError);
        client.next_layer().close(ignoredError);
    }
    catch (...)
    {
        const std::exception_ptr clientException =
            std::current_exception();
        server.Stop();
        services.ioContext.stop();
        serverThread.join();

        if (serverException)
        {
            std::rethrow_exception(serverException);
        }

        std::rethrow_exception(clientException);
    }

    server.Stop();
    services.ioContext.stop();
    serverThread.join();

    if (serverException)
    {
        std::rethrow_exception(serverException);
    }
}
} // namespace

int main()
{
    TestTlsConfigurationIsRequired();
    TestServerAcceptsTlsConnection();

    std::cout << "All auth TLS server tests passed.\n";
    return 0;
}
