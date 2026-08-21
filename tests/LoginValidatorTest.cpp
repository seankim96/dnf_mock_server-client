#include "LoginValidator.h"

#include <cassert>
#include <iostream>
#include <string>

namespace
{
void TestValidName()
{
    const dnf::LoginValidator validator;
    const auto result = validator.Validate("Mock_Player1");

    assert(result.result == dnf::ValidPlayerName);
    assert(result.playerName == "Mock_Player1");
}

void TestEmptyName()
{
    const dnf::LoginValidator validator;
    const auto result = validator.Validate("");

    assert(result.result == dnf::EmptyPlayerName);
}

void TestLongName()
{
    const dnf::LoginValidator validator;
    const std::string longName(dnf::MAX_PLAYER_NAME_LENGTH + 1, 'A');
    const auto result = validator.Validate(longName);

    assert(result.result == dnf::PlayerNameTooLong);
}

void TestInvalidCharacter()
{
    const dnf::LoginValidator validator;
    const auto result = validator.Validate("Bad Name");

    assert(result.result == dnf::InvalidPlayerNameCharacter);
}
} // namespace

int main()
{
    TestValidName();
    TestEmptyName();
    TestLongName();
    TestInvalidCharacter();

    std::cout << "All login validator tests passed.\n";
    return 0;
}
