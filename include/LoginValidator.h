#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace dnf
{
constexpr std::size_t MAX_PLAYER_NAME_LENGTH = 16;

enum PlayerNameValidationResult : std::uint8_t
{
    ValidPlayerName = 0,
    EmptyPlayerName = 1,
    PlayerNameTooLong = 2,
    InvalidPlayerNameCharacter = 3
};

struct LoginValidationResult
{
    PlayerNameValidationResult result = ValidPlayerName;
    std::string playerName;
};

class LoginValidator
{
public:
    LoginValidationResult Validate(
        const std::string& playerName) const;
};
} // namespace dnf
