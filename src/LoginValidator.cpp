#include "LoginValidator.h"

namespace dnf
{
namespace
{
bool IsAllowedCharacter(char character)
{
    const bool isUppercase = character >= 'A' && character <= 'Z';
    const bool isLowercase = character >= 'a' && character <= 'z';
    const bool isNumber = character >= '0' && character <= '9';

    return isUppercase || isLowercase || isNumber || character == '_';
}
} // namespace

LoginValidationResult LoginValidator::Validate(
    const std::string& playerName) const
{
    if (playerName.empty())
    {
        return {EmptyPlayerName, {}};
    }

    if (playerName.size() > MAX_PLAYER_NAME_LENGTH)
    {
        return {PlayerNameTooLong, {}};
    }

    for (char character : playerName)
    {
        if (!IsAllowedCharacter(character))
        {
            return {InvalidPlayerNameCharacter, {}};
        }
    }

    return {ValidPlayerName, playerName};
}
} // namespace dnf
