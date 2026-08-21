#include "AuthPacketDispatcher.h"

#include "AccountAuthenticationService.h"
#include "AuthFlatBufferCodec.h"

#include <flatbuffers/flatbuffer_builder.h>

#include <openssl/crypto.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace dnf
{
namespace protocol = Dnf::Protocol::Auth;

namespace
{
class PayloadCleanup
{
public:
    explicit PayloadCleanup(std::vector<std::uint8_t>& bytes)
        : bytes_(bytes)
    {
    }

    ~PayloadCleanup()
    {
        if (!bytes_.empty())
        {
            OPENSSL_cleanse(bytes_.data(), bytes_.size());
        }
    }

private:
    std::vector<std::uint8_t>& bytes_;
};

protocol::LoginResult ToProtocolResult(
    AccountAuthenticationStatus status)
{
    switch (status)
    {
    case AccountAuthenticationStatus::Success:
        return protocol::LoginResult_Success;

    case AccountAuthenticationStatus::InvalidCredentials:
        return protocol::LoginResult_InvalidCredentials;

    case AccountAuthenticationStatus::ServiceError:
        return protocol::LoginResult_ServiceError;
    }

    throw std::runtime_error("Unknown account authentication status");
}

std::vector<std::uint8_t> EncodeLoginResponse(
    std::uint32_t requestId,
    protocol::LoginResult result)
{
    flatbuffers::FlatBufferBuilder builder;
    const auto response = protocol::CreateLoginResponse(builder, result);
    const auto payload = FinishAuthPayload(
        builder,
        protocol::AuthPayload_LoginResponse,
        response.Union());

    return EncodePacket(AuthLoginResponse, requestId, payload);
}
} // namespace

AuthPacketDispatcher::AuthPacketDispatcher(
    AccountAuthenticationService& authenticationService)
    : authenticationService_(authenticationService),
      sessionState_(std::make_shared<AuthServerSessionState>()),
      loginInProgress_(std::make_shared<std::atomic_bool>(false))
{
}

void AuthPacketDispatcher::DispatchAsync(
    Packet request,
    ResponseHandler responseHandler) const
{
    if (!responseHandler)
    {
        throw std::invalid_argument("Response handler is required");
    }

    if (request.header.type != AuthLoginRequest)
    {
        throw std::runtime_error("No auth handler for packet type");
    }

    if (sessionState_->IsAuthenticated())
    {
        throw std::runtime_error("Auth session is already authenticated");
    }

    HandleLoginRequestAsync(
        std::move(request),
        std::move(responseHandler));
}

std::optional<AccountId>
AuthPacketDispatcher::AuthenticatedAccount() const
{
    return sessionState_->AuthenticatedAccount();
}

void AuthPacketDispatcher::HandleLoginRequestAsync(
    Packet request,
    ResponseHandler responseHandler) const
{
    PayloadCleanup payloadCleanup(request.payload);

    if (loginInProgress_->exchange(true))
    {
        throw std::runtime_error("Auth login is already in progress");
    }

    const auto sessionState = sessionState_;
    const auto loginInProgress = loginInProgress_;

    try
    {
        const auto* message = DecodeAuthPayload(
            request.payload,
            protocol::AuthPayload_LoginRequest);
        const auto* loginRequest = message->payload_as_LoginRequest();

        std::string loginId = loginRequest->login_id()->str();
        std::string password = loginRequest->password()->str();
        const std::uint32_t requestId = request.header.requestId;

        authenticationService_.Authenticate(
            std::move(loginId),
            std::move(password),
            [requestId,
             sessionState,
             loginInProgress,
             responseHandler = std::move(responseHandler)](
                AccountAuthenticationResult authenticationResult) mutable
            {
                protocol::LoginResult responseResult =
                    ToProtocolResult(authenticationResult.status);

                if (responseResult == protocol::LoginResult_Success &&
                    (!authenticationResult.accountId.has_value() ||
                     !sessionState->MarkAuthenticated(
                         authenticationResult.accountId.value())))
                {
                    responseResult = protocol::LoginResult_ServiceError;
                }

                loginInProgress->store(false);
                responseHandler(EncodeLoginResponse(
                    requestId,
                    responseResult));
            });
    }
    catch (...)
    {
        loginInProgress_->store(false);
        throw;
    }
}
} // namespace dnf
