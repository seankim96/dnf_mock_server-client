#include "AccountAuthenticationService.h"
#include "AuthFlatBufferCodec.h"
#include "AuthPacketDispatcher.h"
#include "AuthTicketIssuer.h"
#include "AuthTlsSession.h"
#include "CharacterListService.h"
#include "CharacterSelectionService.h"
#include "DatabaseExecutor.h"
#include "Packet.h"
#include "PasswordHasher.h"
#include "SqliteAccountPlayerRepository.h"
#include "SqliteAccountRepository.h"
#include "SqliteAuthTicketStore.h"
#include "SqliteDatabase.h"
#include "SqlitePlayerRepository.h"
#include "TestTlsCertificate.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>

#include <flatbuffers/flatbuffer_builder.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
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

std::vector<std::uint8_t> MakeCharacterListPacket()
{
    flatbuffers::FlatBufferBuilder builder;
    const auto request = protocol::CreateCharacterListRequest(builder);
    const auto payload = dnf::FinishAuthPayload(
        builder,
        protocol::AuthPayload_CharacterListRequest,
        request.Union());

    return dnf::EncodePacket(
        dnf::AuthCharacterListRequest,
        2,
        payload);
}

dnf::Packet ReadPacket(
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

void TestPipelinedRequestsAreHandledSequentially()
{
    boost::asio::io_context serverIoContext;
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountRepository accountRepository(database);
    dnf::SqlitePlayerRepository playerRepository(database);
    dnf::SqliteAccountPlayerRepository accountPlayerRepository(database);
    dnf::SqliteAuthTicketStore ticketStore(database);
    dnf::AuthTicketIssuer ticketIssuer(
        accountPlayerRepository,
        ticketStore);
    TestPasswordHasher passwordHasher;
    dnf::DatabaseExecutor databaseExecutor(1);
    dnf::AccountAuthenticationService authenticationService(
        serverIoContext,
        databaseExecutor,
        accountRepository,
        passwordHasher);
    dnf::CharacterListService characterListService(
        serverIoContext,
        databaseExecutor,
        accountRepository,
        accountPlayerRepository,
        playerRepository);
    dnf::CharacterSelectionService characterSelectionService(
        serverIoContext,
        databaseExecutor,
        ticketIssuer);

    const dnf::Account account = accountRepository.CreateAccount(
        "account_1",
        passwordHasher.Hash("correct-password").value()).value();
    const dnf::PlayerProfile player =
        playerRepository.CreatePlayer("Player_1").value();
    assert(accountPlayerRepository.LinkPlayer(
        account.accountId,
        player.playerId));

    boost::asio::ssl::context serverTlsContext(
        boost::asio::ssl::context::tls_server);
    dnf::test::ConfigureTestTlsContext(serverTlsContext);

    tcp::acceptor acceptor(
        serverIoContext,
        tcp::endpoint(tcp::v4(), 0));
    const std::uint16_t port = acceptor.local_endpoint().port();

    acceptor.async_accept(
        [&](const boost::system::error_code& error, tcp::socket socket)
        {
            if (error)
            {
                throw boost::system::system_error(error);
            }

            boost::system::error_code ignoredError;
            acceptor.close(ignoredError);

            std::make_shared<dnf::AuthTlsSession>(
                std::move(socket),
                serverTlsContext,
                authenticationService,
                characterListService,
                characterSelectionService,
                dnf::GameServerAddress{"127.0.0.1", 7777})
                ->Start();
        });

    std::exception_ptr serverException;
    auto serverWorkGuard =
        boost::asio::make_work_guard(serverIoContext);
    std::thread serverThread(
        [&]
        {
            try
            {
                serverIoContext.run();
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

        std::vector<std::uint8_t> sentBytes = MakeLoginPacket();
        const std::vector<std::uint8_t> listPacket =
            MakeCharacterListPacket();
        sentBytes.insert(
            sentBytes.end(),
            listPacket.begin(),
            listPacket.end());
        boost::asio::write(client, boost::asio::buffer(sentBytes));

        const dnf::Packet loginResponse = ReadPacket(client);
        assert(loginResponse.header.type == dnf::AuthLoginResponse);
        assert(loginResponse.header.requestId == 1);
        const auto* loginMessage = dnf::DecodeAuthPayload(
            loginResponse.payload,
            protocol::AuthPayload_LoginResponse);
        assert(loginMessage->payload_as_LoginResponse()->result() ==
               protocol::LoginResult_Success);

        const dnf::Packet listResponse = ReadPacket(client);
        assert(listResponse.header.type ==
               dnf::AuthCharacterListResponse);
        assert(listResponse.header.requestId == 2);
        const auto* listMessage = dnf::DecodeAuthPayload(
            listResponse.payload,
            protocol::AuthPayload_CharacterListResponse);
        const auto* characterList =
            listMessage->payload_as_CharacterListResponse();
        assert(characterList->result() ==
               protocol::CharacterListResult_Success);
        assert(characterList->characters()->size() == 1);
        assert(characterList->characters()->Get(0)->player_id() ==
               player.playerId);

        boost::system::error_code ignoredError;
        client.shutdown(ignoredError);
        client.next_layer().close(ignoredError);
    }
    catch (...)
    {
        const std::exception_ptr clientException =
            std::current_exception();
        serverWorkGuard.reset();
        serverIoContext.stop();
        serverThread.join();

        if (serverException)
        {
            std::rethrow_exception(serverException);
        }

        std::rethrow_exception(clientException);
    }

    serverWorkGuard.reset();
    serverIoContext.stop();
    serverThread.join();

    if (serverException)
    {
        std::rethrow_exception(serverException);
    }
}
} // namespace

int main()
{
    TestPipelinedRequestsAreHandledSequentially();

    std::cout << "All auth TLS session tests passed.\n";
    return 0;
}
