#include "AccountAuthenticationService.h"
#include "AccountProvisioningService.h"
#include "AuthFlatBufferCodec.h"
#include "AuthTicketIssuer.h"
#include "AuthTlsServer.h"
#include "CharacterListService.h"
#include "CharacterSelectionService.h"
#include "DatabaseExecutor.h"
#include "NetworkSessionOptions.h"
#include "Packet.h"
#include "PasswordHasher.h"
#include "SqliteAccountPlayerRepository.h"
#include "SqliteAccountRepository.h"
#include "SqliteAuthTicketStore.h"
#include "SqliteDatabase.h"
#include "SqlitePlayerRepository.h"
#include "TestTlsCertificate.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>

#include <flatbuffers/flatbuffer_builder.h>

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace protocol = Dnf::Protocol::Auth;
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

class AuthServerHarness
{
public:
    AuthServerHarness(
        TestServices& services,
        dnf::NetworkSessionOptions options)
        : services_(services),
          server_(
              services.ioContext,
              0,
              certificateFiles_.CertificatePath(),
              certificateFiles_.PrivateKeyPath(),
              services.authenticationService,
              services.characterListService,
              services.characterSelectionService,
              {"127.0.0.1", 7777},
              std::move(options))
    {
        server_.Start();
        serverThread_ = std::thread(
            [this]
            {
                services_.ioContext.run();
            });
    }

    ~AuthServerHarness()
    {
        server_.Stop();
        services_.ioContext.stop();

        if (serverThread_.joinable())
        {
            serverThread_.join();
        }
    }

    std::uint16_t Port() const
    {
        return server_.Port();
    }

private:
    TestServices& services_;
    dnf::test::TestTlsCertificateFiles certificateFiles_;
    dnf::AuthTlsServer server_;
    std::thread serverThread_;
};

std::vector<std::uint8_t> MakeLoginPacket()
{
    flatbuffers::FlatBufferBuilder builder;
    const auto loginId = builder.CreateString("account_1");
    const auto password = builder.CreateString("correct-password");
    const auto request =
        protocol::CreateLoginRequest(builder, loginId, password);
    const auto payload = dnf::FinishAuthPayload(
        builder,
        protocol::AuthPayload_LoginRequest,
        request.Union());

    return dnf::EncodePacket(dnf::AuthLoginRequest, 1, payload);
}

dnf::Packet ReadTlsPacket(
    boost::asio::ssl::stream<tcp::socket>& client)
{
    std::array<std::uint8_t, dnf::PACKET_HEADER_SIZE> headerBytes{};
    boost::asio::read(client, boost::asio::buffer(headerBytes));

    dnf::Packet packet;
    packet.header = dnf::DecodeHeader(headerBytes);
    packet.payload.resize(
        packet.header.packetSize - dnf::PACKET_HEADER_SIZE);

    if (!packet.payload.empty())
    {
        boost::asio::read(client, boost::asio::buffer(packet.payload));
    }

    return packet;
}

template <typename Socket>
boost::system::error_code WaitForClose(
    boost::asio::io_context& ioContext,
    Socket& socket,
    tcp::socket& nextLayer)
{
    std::array<std::uint8_t, 1> byte{};
    std::optional<boost::system::error_code> readError;
    boost::asio::steady_timer testTimeout(
        ioContext,
        std::chrono::seconds(2));

    socket.async_read_some(
        boost::asio::buffer(byte),
        [&](const boost::system::error_code& error, std::size_t)
        {
            readError = error;
            testTimeout.cancel();
        });
    testTimeout.async_wait(
        [&](const boost::system::error_code& error)
        {
            if (!error)
            {
                nextLayer.cancel();
            }
        });

    ioContext.restart();
    ioContext.run();
    return readError.value_or(
        boost::asio::error::operation_aborted);
}

void ProvisionTestAccount(TestServices& services)
{
    dnf::AccountProvisioningService accountProvisioner(
        services.accountRepository,
        services.passwordHasher);
    const dnf::AccountProvisioningResult result =
        accountProvisioner.CreateAccount(
            "account_1",
            "correct-password");
    assert(result.status ==
           dnf::AccountProvisioningStatus::Success);
}

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

void TestHandshakeTimeoutClosesConnection()
{
    TestServices services;
    dnf::NetworkSessionOptions options;
    options.handshakeTimeout = std::chrono::milliseconds(50);
    options.authenticationTimeout = std::chrono::seconds(1);
    options.readTimeout = std::chrono::seconds(1);
    AuthServerHarness server(services, options);

    boost::asio::io_context clientIoContext;
    tcp::socket client(clientIoContext);
    client.connect(tcp::endpoint(
        boost::asio::ip::address_v4::loopback(),
        server.Port()));

    const boost::system::error_code closeError =
        WaitForClose(clientIoContext, client, client);
    assert(closeError != boost::asio::error::operation_aborted);
}

void TestAuthenticationTimeoutClosesTlsConnection()
{
    TestServices services;
    dnf::NetworkSessionOptions options;
    options.handshakeTimeout = std::chrono::seconds(1);
    options.authenticationTimeout = std::chrono::milliseconds(50);
    options.readTimeout = std::chrono::seconds(1);
    AuthServerHarness server(services, options);

    boost::asio::io_context clientIoContext;
    boost::asio::ssl::context clientTlsContext(
        boost::asio::ssl::context::tls_client);
    clientTlsContext.set_verify_mode(boost::asio::ssl::verify_none);
    boost::asio::ssl::stream<tcp::socket> client(
        clientIoContext,
        clientTlsContext);
    client.next_layer().connect(tcp::endpoint(
        boost::asio::ip::address_v4::loopback(),
        server.Port()));
    client.handshake(boost::asio::ssl::stream_base::client);

    const boost::system::error_code closeError = WaitForClose(
        clientIoContext,
        client,
        client.next_layer());
    assert(closeError != boost::asio::error::operation_aborted);
}

void TestReadTimeoutAfterTlsLogin()
{
    TestServices services;
    ProvisionTestAccount(services);

    dnf::NetworkSessionOptions options;
    options.handshakeTimeout = std::chrono::seconds(1);
    options.authenticationTimeout = std::chrono::seconds(1);
    options.readTimeout = std::chrono::milliseconds(50);
    AuthServerHarness server(services, options);

    boost::asio::io_context clientIoContext;
    boost::asio::ssl::context clientTlsContext(
        boost::asio::ssl::context::tls_client);
    clientTlsContext.set_verify_mode(boost::asio::ssl::verify_none);
    boost::asio::ssl::stream<tcp::socket> client(
        clientIoContext,
        clientTlsContext);
    client.next_layer().connect(tcp::endpoint(
        boost::asio::ip::address_v4::loopback(),
        server.Port()));
    client.handshake(boost::asio::ssl::stream_base::client);

    const std::vector<std::uint8_t> loginRequest = MakeLoginPacket();
    boost::asio::write(client, boost::asio::buffer(loginRequest));

    const dnf::Packet loginResponse = ReadTlsPacket(client);
    assert(loginResponse.header.type == dnf::AuthLoginResponse);
    const auto* message = dnf::DecodeAuthPayload(
        loginResponse.payload,
        protocol::AuthPayload_LoginResponse);
    assert(message->payload_as_LoginResponse()->result() ==
           protocol::LoginResult_Success);

    const boost::system::error_code closeError = WaitForClose(
        clientIoContext,
        client,
        client.next_layer());
    assert(closeError != boost::asio::error::operation_aborted);
}

void TestTlsSessionRejectsOversizedPendingWrite()
{
    TestServices services;
    ProvisionTestAccount(services);

    dnf::NetworkSessionOptions options;
    options.handshakeTimeout = std::chrono::seconds(1);
    options.authenticationTimeout = std::chrono::seconds(1);
    options.readTimeout = std::chrono::seconds(1);
    options.maxPendingWriteBytes = dnf::PACKET_HEADER_SIZE;
    AuthServerHarness server(services, options);

    boost::asio::io_context clientIoContext;
    boost::asio::ssl::context clientTlsContext(
        boost::asio::ssl::context::tls_client);
    clientTlsContext.set_verify_mode(boost::asio::ssl::verify_none);
    boost::asio::ssl::stream<tcp::socket> client(
        clientIoContext,
        clientTlsContext);
    client.next_layer().connect(tcp::endpoint(
        boost::asio::ip::address_v4::loopback(),
        server.Port()));
    client.handshake(boost::asio::ssl::stream_base::client);

    const std::vector<std::uint8_t> loginRequest = MakeLoginPacket();
    boost::asio::write(client, boost::asio::buffer(loginRequest));

    const boost::system::error_code closeError = WaitForClose(
        clientIoContext,
        client,
        client.next_layer());
    assert(closeError != boost::asio::error::operation_aborted);
}
} // namespace

int main()
{
    TestTlsConfigurationIsRequired();
    TestServerAcceptsTlsConnection();
    TestHandshakeTimeoutClosesConnection();
    TestAuthenticationTimeoutClosesTlsConnection();
    TestReadTimeoutAfterTlsLogin();
    TestTlsSessionRejectsOversizedPendingWrite();

    std::cout << "All auth TLS server tests passed.\n";
    return 0;
}
