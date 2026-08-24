#pragma once

#include "AuthTicketVerifier.h"
#include "SessionId.h"

#include <cstdint>
#include <string>
#include <vector>

namespace dnf
{
enum LoginResult : std::uint8_t
{
    LoginSuccess = 0,
    InvalidAuthTicket = 1,
    LoginPlayerNotFound = 2,
    LoginStorageError = 3,
    LoginServiceBusy = 4
};

struct LoginResponseData
{
    LoginResult result = LoginSuccess;
    SessionId sessionId = 0;
};

std::vector<std::uint8_t> EncodeLoginRequestPayload(
    const std::string& authTicket);

std::string DecodeLoginRequestPayload(
    const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> EncodeLoginResponsePayload(
    LoginResult result,
    SessionId sessionId);

LoginResponseData DecodeLoginResponsePayload(
    const std::vector<std::uint8_t>& payload);
} // namespace dnf
