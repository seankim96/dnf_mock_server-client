#include "AccountAuthenticationService.h"
#include "AuthFlatBufferCodec.h"
#include "AuthPacketDispatcher.h"
#include "DatabaseExecutor.h"
#include "PasswordHasher.h"
#include "ReceiveBuffer.h"
#include "SqliteAccountRepository.h"
#include "SqliteDatabase.h"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include <flatbuffers/flatbuffer_builder.h>

#include <cassert>
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

void TestSuccessfulLoginStoresAccount()
{
    boost::asio::io_context ioContext;
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountRepository accountRepository(database);
    TestPasswordHasher passwordHasher;
    const auto account = accountRepository.CreateAccount(
        "account_1",
        passwordHasher.Hash("correct-password").value());
    assert(account.has_value());
    dnf::DatabaseExecutor databaseExecutor(1);
    dnf::AccountAuthenticationService authenticationService(
        ioContext,
        databaseExecutor,
        accountRepository,
        passwordHasher);
    dnf::AuthPacketDispatcher dispatcher(authenticationService);

    const LoginResponseData response = WaitForLogin(
        dispatcher,
        ioContext,
        MakeLoginRequest(77, "account_1", "correct-password"));

    assert(response.requestId == 77);
    assert(response.result == protocol::LoginResult_Success);
    assert(dispatcher.AuthenticatedAccount() == account->accountId);

    bool rejected = false;
    try
    {
        dispatcher.DispatchAsync(
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
    boost::asio::io_context ioContext;
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountRepository accountRepository(database);
    TestPasswordHasher passwordHasher;
    assert(accountRepository.CreateAccount(
        "account_1",
        passwordHasher.Hash("correct-password").value()));
    dnf::DatabaseExecutor databaseExecutor(1);
    dnf::AccountAuthenticationService authenticationService(
        ioContext,
        databaseExecutor,
        accountRepository,
        passwordHasher);
    dnf::AuthPacketDispatcher dispatcher(authenticationService);

    const LoginResponseData failed = WaitForLogin(
        dispatcher,
        ioContext,
        MakeLoginRequest(1, "account_1", "wrong-password"));
    assert(failed.result == protocol::LoginResult_InvalidCredentials);
    assert(!dispatcher.AuthenticatedAccount().has_value());

    const LoginResponseData retried = WaitForLogin(
        dispatcher,
        ioContext,
        MakeLoginRequest(2, "account_1", "correct-password"));
    assert(retried.result == protocol::LoginResult_Success);
    assert(dispatcher.AuthenticatedAccount().has_value());
}

void TestSecondLoginWhileFirstIsRunningIsRejected()
{
    boost::asio::io_context ioContext;
    dnf::SqliteDatabase database(":memory:");
    dnf::SqliteAccountRepository accountRepository(database);
    TestPasswordHasher passwordHasher;
    assert(accountRepository.CreateAccount(
        "account_1",
        passwordHasher.Hash("correct-password").value()));
    dnf::DatabaseExecutor databaseExecutor(1);
    dnf::AccountAuthenticationService authenticationService(
        ioContext,
        databaseExecutor,
        accountRepository,
        passwordHasher);
    dnf::AuthPacketDispatcher dispatcher(authenticationService);
    auto workGuard = boost::asio::make_work_guard(ioContext);

    dispatcher.DispatchAsync(
        MakeLoginRequest(1, "account_1", "correct-password"),
        [&](std::vector<std::uint8_t>)
        {
            workGuard.reset();
        });

    bool rejected = false;
    try
    {
        dispatcher.DispatchAsync(
            MakeLoginRequest(2, "account_1", "correct-password"),
            [](std::vector<std::uint8_t>) {});
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }

    assert(rejected);
    ioContext.run();
}
} // namespace

int main()
{
    TestSuccessfulLoginStoresAccount();
    TestFailedLoginCanBeRetried();
    TestSecondLoginWhileFirstIsRunningIsRejected();

    std::cout << "All auth packet dispatcher tests passed.\n";
    return 0;
}
