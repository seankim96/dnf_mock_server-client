#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dnf
{
constexpr std::size_t MAX_PLAYER_NAME_LENGTH = 16;

enum LoginResult : std::uint8_t
{
    LoginSuccess = 0,
    EmptyPlayerName = 1,
    PlayerNameTooLong = 2,
    InvalidPlayerNameCharacter = 3
};

struct LoginValidationResult
{
    LoginResult result = LoginSuccess;
    std::string playerName;
};

class LoginValidator
{
public:
    LoginValidationResult Validate(
        const std::vector<std::uint8_t>& payload) const;
};
} // namespace dnf
