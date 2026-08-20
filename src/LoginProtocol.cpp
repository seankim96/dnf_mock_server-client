#include "LoginProtocol.h"
#include "TcpFlatBufferCodec.h"

#include <flatbuffers/flatbuffer_builder.h>

#include <stdexcept>

namespace dnf
{
namespace
{
namespace tcp = Dnf::Protocol::Tcp;

bool IsValidResult(LoginResult result)
{
    return result >= LoginSuccess &&
           result <= InvalidPlayerNameCharacter;
}

bool IsValidResponse(LoginResult result, SessionId sessionId)
{
    if (!IsValidResult(result))
    {
        return false;
    }

    const bool succeeded = result == LoginSuccess;
    return succeeded == (sessionId != 0);
}
} // namespace

std::vector<std::uint8_t> EncodeLoginRequestPayload(
    const std::string& playerName)
{
    flatbuffers::FlatBufferBuilder builder;
    const auto name = builder.CreateString(playerName);
    const auto request = tcp::CreateLoginRequest(builder, name);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_LoginRequest,
        request.Union());
}

std::string DecodeLoginRequestPayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_LoginRequest);
    const auto* request = message->payload_as_LoginRequest();
    if (request == nullptr || request->player_name() == nullptr)
    {
        throw std::runtime_error("Invalid login request payload");
    }

    return request->player_name()->str();
}

std::vector<std::uint8_t> EncodeLoginResponsePayload(
    LoginResult result,
    SessionId sessionId)
{
    if (!IsValidResponse(result, sessionId))
    {
        throw std::invalid_argument("Invalid login response");
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto response = tcp::CreateLoginResponse(
        builder,
        static_cast<tcp::LoginResult>(result),
        sessionId);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_LoginResponse,
        response.Union());
}

LoginResponseData DecodeLoginResponsePayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_LoginResponse);
    const auto* response = message->payload_as_LoginResponse();
    if (response == nullptr)
    {
        throw std::runtime_error("Invalid login response payload");
    }

    const auto result = static_cast<LoginResult>(response->result());
    const SessionId sessionId = response->session_id();

    if (!IsValidResponse(result, sessionId))
    {
        throw std::runtime_error("Invalid login response data");
    }

    return {result, sessionId};
}
} // namespace dnf
