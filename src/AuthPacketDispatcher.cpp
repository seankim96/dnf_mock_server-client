#include "AuthPacketDispatcher.h"

#include "AccountAuthenticationService.h"
#include "AuthFlatBufferCodec.h"
#include "CharacterListService.h"

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

protocol::CharacterListResult ToProtocolResult(
    CharacterListStatus status)
{
    switch (status)
    {
    case CharacterListStatus::Success:
        return protocol::CharacterListResult_Success;

    case CharacterListStatus::AccountNotFound:
        return protocol::CharacterListResult_AccountNotFound;

    case CharacterListStatus::ServiceError:
        return protocol::CharacterListResult_ServiceError;
    }

    throw std::runtime_error("Unknown character list status");
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

std::vector<std::uint8_t> EncodeCharacterListResponse(
    std::uint32_t requestId,
    protocol::CharacterListResult result,
    const std::vector<CharacterSummary>& characters)
{
    flatbuffers::FlatBufferBuilder builder;
    std::vector<flatbuffers::Offset<protocol::CharacterSummary>>
        encodedCharacters;
    encodedCharacters.reserve(characters.size());

    for (const CharacterSummary& character : characters)
    {
        const auto displayName = builder.CreateString(character.name);
        encodedCharacters.push_back(protocol::CreateCharacterSummary(
            builder,
            character.playerId,
            displayName,
            character.level));
    }

    const auto characterVector =
        builder.CreateVector(encodedCharacters);
    const auto response = protocol::CreateCharacterListResponse(
        builder,
        result,
        characterVector);
    const auto payload = FinishAuthPayload(
        builder,
        protocol::AuthPayload_CharacterListResponse,
        response.Union());

    return EncodePacket(
        AuthCharacterListResponse,
        requestId,
        payload);
}
} // namespace

AuthPacketDispatcher::AuthPacketDispatcher(
    AccountAuthenticationService& authenticationService,
    CharacterListService& characterListService)
    : authenticationService_(authenticationService),
      characterListService_(characterListService),
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

    if (request.header.type == AuthLoginRequest)
    {
        if (sessionState_->IsAuthenticated())
        {
            throw std::runtime_error(
                "Auth session is already authenticated");
        }

        HandleLoginRequestAsync(
            std::move(request),
            std::move(responseHandler));
        return;
    }

    if (request.header.type == AuthCharacterListRequest)
    {
        HandleCharacterListRequestAsync(
            std::move(request),
            std::move(responseHandler));
        return;
    }

    throw std::runtime_error("No auth handler for packet type");
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

void AuthPacketDispatcher::HandleCharacterListRequestAsync(
    Packet request,
    ResponseHandler responseHandler) const
{
    DecodeAuthPayload(
        request.payload,
        protocol::AuthPayload_CharacterListRequest);

    const std::uint32_t requestId = request.header.requestId;
    const std::optional<AccountId> accountId =
        sessionState_->AuthenticatedAccount();

    if (!accountId.has_value())
    {
        responseHandler(EncodeCharacterListResponse(
            requestId,
            protocol::CharacterListResult_NotAuthenticated,
            {}));
        return;
    }

    characterListService_.LoadCharacters(
        accountId.value(),
        [requestId,
         responseHandler = std::move(responseHandler)](
            CharacterListResult listResult) mutable
        {
            const protocol::CharacterListResult responseResult =
                ToProtocolResult(listResult.status);

            if (responseResult !=
                protocol::CharacterListResult_Success)
            {
                listResult.characters.clear();
            }

            responseHandler(EncodeCharacterListResponse(
                requestId,
                responseResult,
                listResult.characters));
        });
}
} // namespace dnf
