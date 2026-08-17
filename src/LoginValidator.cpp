#include "LoginValidator.h"

namespace dnf
{
namespace
{
bool IsAllowedCharacter(std::uint8_t character)
{
    const bool isUppercase = character >= 'A' && character <= 'Z';
    const bool isLowercase = character >= 'a' && character <= 'z';
    const bool isNumber = character >= '0' && character <= '9';

    return isUppercase || isLowercase || isNumber || character == '_';
}
} // namespace

LoginValidationResult LoginValidator::Validate(
    const std::vector<std::uint8_t>& payload) const
{
    if (payload.empty())
    {
        return {EmptyPlayerName, {}};
    }

    if (payload.size() > MAX_PLAYER_NAME_LENGTH)
    {
        return {PlayerNameTooLong, {}};
    }

    for (std::uint8_t character : payload)
    {
        if (!IsAllowedCharacter(character))
        {
            return {InvalidPlayerNameCharacter, {}};
        }
    }

    const std::string playerName(payload.begin(), payload.end());
    return {LoginSuccess, playerName};
}
} // namespace dnf
