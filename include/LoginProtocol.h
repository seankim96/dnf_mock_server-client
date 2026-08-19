#pragma once

#include "LoginValidator.h"
#include "SessionId.h"

#include <cstdint>
#include <vector>

namespace dnf
{
struct LoginResponseData
{
    LoginResult result = LoginSuccess;
    SessionId sessionId = 0;
};

std::vector<std::uint8_t> EncodeLoginResponsePayload(
    LoginResult result,
    SessionId sessionId);

LoginResponseData DecodeLoginResponsePayload(
    const std::vector<std::uint8_t>& payload);
} // namespace dnf
