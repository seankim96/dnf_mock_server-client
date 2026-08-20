#pragma once

#include "LoginValidator.h"
#include "SessionId.h"

#include <cstdint>
#include <string>
#include <vector>

namespace dnf
{
struct LoginResponseData
{
    LoginResult result = LoginSuccess;
    SessionId sessionId = 0;
};

std::vector<std::uint8_t> EncodeLoginRequestPayload(
    const std::string& playerName);

std::string DecodeLoginRequestPayload(
    const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> EncodeLoginResponsePayload(
    LoginResult result,
    SessionId sessionId);

LoginResponseData DecodeLoginResponsePayload(
    const std::vector<std::uint8_t>& payload);
} // namespace dnf
