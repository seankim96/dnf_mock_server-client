#include "AccountAuthenticationService.h"
#include "AuthFlatBufferCodec.h"
#include "AuthPacketDispatcher.h"
#include "AuthTicketIssuer.h"
#include "CharacterListService.h"
#include "CharacterSelectionService.h"
#include "DatabaseExecutor.h"
#include "PasswordHasher.h"
#include "ReceiveBuffer.h"
#include "SqliteAccountPlayerRepository.h"
#include "SqliteAccountRepository.h"
#include "SqliteAuthTicketStore.h"
#include "SqliteAuthTicketVerifier.h"
#include "SqliteDatabase.h"
#include "SqlitePlayerRepository.h"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include <flatbuffers/flatbuffer_builder.h>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace protocol = Dnf::Protocol::Auth;

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

struct TestContext
{
    TestContext()
        : database(":memory:"),
          accountRepository(database),
          playerRepository(database),
          accountPlayerRepository(database),
          ticketStore(database),
          ticketVerifier(ticketStore),
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
              ticketIssuer),
          dispatcher(
              authenticationService,
              characterListService,
              characterSelectionService,
              {"127.0.0.1", 7777})
    {
    }

    dnf::Account CreateAccount(
        const std::string& loginId,
        const std::string& password)
    {
        return accountRepository.CreateAccount(
            loginId,
            passwordHasher.Hash(password).value()).value();
    }

    boost::asio::io_context ioContext;
    dnf::SqliteDatabase database;
    dnf::SqliteAccountRepository accountRepository;
    dnf::SqlitePlayerRepository playerRepository;
    dnf::SqliteAccountPlayerRepository accountPlayerRepository;
    dnf::SqliteAuthTicketStore ticketStore;
    dnf::SqliteAuthTicketVerifier ticketVerifier;
    dnf::AuthTicketIssuer ticketIssuer;
    TestPasswordHasher passwordHasher;
    dnf::DatabaseExecutor databaseExecutor;
    dnf::AccountAuthenticationService authenticationService;
    dnf::CharacterListService characterListService;
    dnf::CharacterSelectionService characterSelectionService;
    dnf::AuthPacketDispatcher dispatcher;
};

dnf::Packet MakeLoginRequest(
    std::uint32_t requestId,
    const std::string& loginId,
    const std::string& password)
{
    flatbuffers::FlatBufferBuilder builder;
    const auto encodedLoginId = builder.CreateString(loginId);
    const auto encodedPassword = builder.CreateString(password);
    const auto request = protocol::CreateLoginRequest(
        builder,
        encodedLoginId,
        encodedPassword);

    dnf::Packet packet;
    packet.header.type = dnf::AuthLoginRequest;
    packet.header.requestId = requestId;
    packet.payload = dnf::FinishAuthPayload(
        builder,
        protocol::AuthPayload_LoginRequest,
        request.Union());
    packet.header.packetSize = static_cast<std::uint16_t>(
        dnf::PACKET_HEADER_SIZE + packet.payload.size());
    return packet;
}

struct LoginResponseData
{
    std::uint32_t requestId = 0;
    protocol::LoginResult result =
        protocol::LoginResult_InvalidCredentials;
};

LoginResponseData DecodeLoginResponse(
    const std::vector<std::uint8_t>& responseBytes)
{
    dnf::ReceiveBuffer receiveBuffer;
    receiveBuffer.Append(responseBytes);

    dnf::Packet response;
    assert(receiveBuffer.TryPop(response));
    assert(response.header.type == dnf::AuthLoginResponse);

    const auto* message = dnf::DecodeAuthPayload(
        response.payload,
        protocol::AuthPayload_LoginResponse);
    const auto* loginResponse = message->payload_as_LoginResponse();
    assert(loginResponse != nullptr);

    return {response.header.requestId, loginResponse->result()};
}

LoginResponseData WaitForLogin(
    dnf::AuthPacketDispatcher& dispatcher,
    boost::asio::io_context& ioContext,
    dnf::Packet request)
{
    auto workGuard = boost::asio::make_work_guard(ioContext);
    std::optional<std::vector<std::uint8_t>> responseBytes;

    dispatcher.DispatchAsync(
        std::move(request),
        [&](std::vector<std::uint8_t> response)
        {
            responseBytes = std::move(response);
            workGuard.reset();
        });

    ioContext.run();
    ioContext.restart();
    assert(responseBytes.has_value());
    return DecodeLoginResponse(responseBytes.value());
}

dnf::Packet MakeCharacterListRequest(std::uint32_t requestId)
{
    flatbuffers::FlatBufferBuilder builder;
    const auto request = protocol::CreateCharacterListRequest(builder);

    dnf::Packet packet;
    packet.header.type = dnf::AuthCharacterListRequest;
    packet.header.requestId = requestId;
    packet.payload = dnf::FinishAuthPayload(
        builder,
        protocol::AuthPayload_CharacterListRequest,
        request.Union());
    packet.header.packetSize = static_cast<std::uint16_t>(
        dnf::PACKET_HEADER_SIZE + packet.payload.size());
    return packet;
}

struct CharacterData
{
    std::uint64_t playerId = 0;
    std::string name;
    std::uint32_t level = 0;
};

struct CharacterListResponseData
{
    std::uint32_t requestId = 0;
    protocol::CharacterListResult result =
        protocol::CharacterListResult_ServiceError;
    std::vector<CharacterData> characters;
};

CharacterListResponseData DecodeCharacterListResponse(
    const std::vector<std::uint8_t>& responseBytes)
{
    dnf::ReceiveBuffer receiveBuffer;
    receiveBuffer.Append(responseBytes);

    dnf::Packet response;
    assert(receiveBuffer.TryPop(response));
    assert(response.header.type == dnf::AuthCharacterListResponse);

    const auto* message = dnf::DecodeAuthPayload(
        response.payload,
        protocol::AuthPayload_CharacterListResponse);
    const auto* listResponse =
        message->payload_as_CharacterListResponse();
    assert(listResponse != nullptr);

    CharacterListResponseData output;
    output.requestId = response.header.requestId;
    output.result = listResponse->result();

    for (const auto* character : *listResponse->characters())
    {
        output.characters.push_back({
            character->player_id(),
            character->display_name()->str(),
            character->level()});
    }

    return output;
}

CharacterListResponseData WaitForCharacterList(
    dnf::AuthPacketDispatcher& dispatcher,
    boost::asio::io_context& ioContext,
    dnf::Packet request)
{
    auto workGuard = boost::asio::make_work_guard(ioContext);
    std::optional<std::vector<std::uint8_t>> responseBytes;

    dispatcher.DispatchAsync(
        std::move(request),
        [&](std::vector<std::uint8_t> response)
        {
            responseBytes = std::move(response);
            workGuard.reset();
        });

    ioContext.run();
    ioContext.restart();
    assert(responseBytes.has_value());
    return DecodeCharacterListResponse(responseBytes.value());
}

dnf::Packet MakeCharacterSelectionRequest(
    std::uint32_t requestId,
    std::uint64_t playerId)
{
    flatbuffers::FlatBufferBuilder builder;
    const auto request = protocol::CreateCharacterSelectionRequest(
        builder,
        playerId);

    dnf::Packet packet;
    packet.header.type = dnf::AuthCharacterSelectionRequest;
    packet.header.requestId = requestId;
    packet.payload = dnf::FinishAuthPayload(
        builder,
        protocol::AuthPayload_CharacterSelectionRequest,
        request.Union());
    packet.header.packetSize = static_cast<std::uint16_t>(
        dnf::PACKET_HEADER_SIZE + packet.payload.size());
    return packet;
}

struct CharacterSelectionResponseData
{
    std::uint32_t requestId = 0;
    protocol::CharacterSelectionResult result =
        protocol::CharacterSelectionResult_ServiceError;
    std::string gameServerHost;
    std::uint16_t gameServerPort = 0;
    std::string authTicket;
    std::int64_t expiresAtUnix = 0;
};

CharacterSelectionResponseData DecodeCharacterSelectionResponse(
    const std::vector<std::uint8_t>& responseBytes)
{
    dnf::ReceiveBuffer receiveBuffer;
    receiveBuffer.Append(responseBytes);

    dnf::Packet response;
    assert(receiveBuffer.TryPop(response));
    assert(response.header.type ==
           dnf::AuthCharacterSelectionResponse);

    const auto* message = dnf::DecodeAuthPayload(
        response.payload,
        protocol::AuthPayload_CharacterSelectionResponse);
    const auto* selectionResponse =
        message->payload_as_CharacterSelectionResponse();
    assert(selectionResponse != nullptr);

    return {
        response.header.requestId,
        selectionResponse->result(),
        selectionResponse->game_server_host()->str(),
        selectionResponse->game_server_port(),
        selectionResponse->auth_ticket()->str(),
        selectionResponse->expires_at_unix()};
}

CharacterSelectionResponseData WaitForCharacterSelection(
    dnf::AuthPacketDispatcher& dispatcher,
    boost::asio::io_context& ioContext,
    dnf::Packet request)
{
    auto workGuard = boost::asio::make_work_guard(ioContext);
    std::optional<std::vector<std::uint8_t>> responseBytes;

    dispatcher.DispatchAsync(
        std::move(request),
        [&](std::vector<std::uint8_t> response)
        {
            responseBytes = std::move(response);
            workGuard.reset();
        });

    ioContext.run();
    ioContext.restart();
    assert(responseBytes.has_value());
    return DecodeCharacterSelectionResponse(responseBytes.value());
}

void TestSuccessfulLoginStoresAccount()
{
    TestContext context;
    const dnf::Account account = context.CreateAccount(
        "account_1", "correct-password");

    const LoginResponseData response = WaitForLogin(
        context.dispatcher,
        context.ioContext,
        MakeLoginRequest(77, "account_1", "correct-password"));

    assert(response.requestId == 77);
    assert(response.result == protocol::LoginResult_Success);
    assert(context.dispatcher.AuthenticatedAccount() ==
           account.accountId);

    bool rejected = false;
    try
    {
        context.dispatcher.DispatchAsync(
            MakeLoginRequest(78, "account_1", "correct-password"),
            [](std::vector<std::uint8_t>) {});
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }

    assert(rejected);
}

void TestFailedLoginCanBeRetried()
{
    TestContext context;
    context.CreateAccount("account_1", "correct-password");

    const LoginResponseData failed = WaitForLogin(
        context.dispatcher,
        context.ioContext,
        MakeLoginRequest(1, "account_1", "wrong-password"));
    assert(failed.result == protocol::LoginResult_InvalidCredentials);
    assert(!context.dispatcher.AuthenticatedAccount().has_value());

    const LoginResponseData retried = WaitForLogin(
        context.dispatcher,
        context.ioContext,
        MakeLoginRequest(2, "account_1", "correct-password"));
    assert(retried.result == protocol::LoginResult_Success);
    assert(context.dispatcher.AuthenticatedAccount().has_value());
}

void TestSecondLoginWhileFirstIsRunningIsRejected()
{
    TestContext context;
    context.CreateAccount("account_1", "correct-password");
    auto workGuard = boost::asio::make_work_guard(context.ioContext);

    context.dispatcher.DispatchAsync(
        MakeLoginRequest(1, "account_1", "correct-password"),
        [&](std::vector<std::uint8_t>)
        {
            workGuard.reset();
        });

    bool rejected = false;
    try
    {
        context.dispatcher.DispatchAsync(
            MakeLoginRequest(2, "account_1", "correct-password"),
            [](std::vector<std::uint8_t>) {});
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }

    assert(rejected);
    context.ioContext.run();
}

void TestCharacterListRequiresAuthentication()
{
    TestContext context;

    const CharacterListResponseData response = WaitForCharacterList(
        context.dispatcher,
        context.ioContext,
        MakeCharacterListRequest(90));

    assert(response.requestId == 90);
    assert(response.result ==
           protocol::CharacterListResult_NotAuthenticated);
    assert(response.characters.empty());
}

void TestCharacterListReturnsOwnedCharacters()
{
    TestContext context;
    const dnf::Account account = context.CreateAccount(
        "account_1", "correct-password");
    dnf::PlayerProfile player =
        context.playerRepository.CreatePlayer("Player_1").value();
    player.level = 25;
    assert(context.playerRepository.SavePlayer(player));
    assert(context.accountPlayerRepository.LinkPlayer(
        account.accountId,
        player.playerId));

    const LoginResponseData login = WaitForLogin(
        context.dispatcher,
        context.ioContext,
        MakeLoginRequest(1, "account_1", "correct-password"));
    assert(login.result == protocol::LoginResult_Success);

    const CharacterListResponseData response = WaitForCharacterList(
        context.dispatcher,
        context.ioContext,
        MakeCharacterListRequest(91));

    assert(response.requestId == 91);
    assert(response.result == protocol::CharacterListResult_Success);
    assert(response.characters.size() == 1);
    assert(response.characters[0].playerId == player.playerId);
    assert(response.characters[0].name == "Player_1");
    assert(response.characters[0].level == 25);
}

void TestCharacterSelectionRequiresAuthentication()
{
    TestContext context;

    const CharacterSelectionResponseData response =
        WaitForCharacterSelection(
            context.dispatcher,
            context.ioContext,
            MakeCharacterSelectionRequest(100, 1));

    assert(response.requestId == 100);
    assert(response.result ==
           protocol::CharacterSelectionResult_NotAuthenticated);
    assert(response.gameServerHost.empty());
    assert(response.gameServerPort == 0);
    assert(response.authTicket.empty());
    assert(response.expiresAtUnix == 0);
}

void TestOwnedCharacterSelectionReturnsGameTicket()
{
    TestContext context;
    const dnf::Account account = context.CreateAccount(
        "account_1", "correct-password");
    const dnf::PlayerProfile ownedPlayer =
        context.playerRepository.CreatePlayer("Player_1").value();
    const dnf::PlayerProfile unownedPlayer =
        context.playerRepository.CreatePlayer("Player_2").value();
    assert(context.accountPlayerRepository.LinkPlayer(
        account.accountId,
        ownedPlayer.playerId));

    const LoginResponseData login = WaitForLogin(
        context.dispatcher,
        context.ioContext,
        MakeLoginRequest(1, "account_1", "correct-password"));
    assert(login.result == protocol::LoginResult_Success);

    const CharacterSelectionResponseData success =
        WaitForCharacterSelection(
            context.dispatcher,
            context.ioContext,
            MakeCharacterSelectionRequest(101, ownedPlayer.playerId));

    assert(success.requestId == 101);
    assert(success.result ==
           protocol::CharacterSelectionResult_Success);
    assert(success.gameServerHost == "127.0.0.1");
    assert(success.gameServerPort == 7777);
    assert(!success.authTicket.empty());
    assert(success.expiresAtUnix > 0);

    const std::optional<dnf::AuthContext> verified =
        context.ticketVerifier.Verify(success.authTicket);
    assert(verified.has_value());
    assert(verified->accountId == account.accountId);
    assert(verified->playerId == ownedPlayer.playerId);

    const CharacterSelectionResponseData rejected =
        WaitForCharacterSelection(
            context.dispatcher,
            context.ioContext,
            MakeCharacterSelectionRequest(
                102,
                unownedPlayer.playerId));

    assert(rejected.result ==
           protocol::CharacterSelectionResult_InvalidSelection);
    assert(rejected.gameServerHost.empty());
    assert(rejected.gameServerPort == 0);
    assert(rejected.authTicket.empty());
    assert(rejected.expiresAtUnix == 0);
}
} // namespace

int main()
{
    TestSuccessfulLoginStoresAccount();
    TestFailedLoginCanBeRetried();
    TestSecondLoginWhileFirstIsRunningIsRejected();
    TestCharacterListRequiresAuthentication();
    TestCharacterListReturnsOwnedCharacters();
    TestCharacterSelectionRequiresAuthentication();
    TestOwnedCharacterSelectionReturnsGameTicket();

    std::cout << "All auth packet dispatcher tests passed.\n";
    return 0;
}
