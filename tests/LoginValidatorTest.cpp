#include "LoginValidator.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
std::vector<std::uint8_t> ToPayload(const std::string& text)
{
    return {text.begin(), text.end()};
}

void TestValidName()
{
    const dnf::LoginValidator validator;
    const auto result = validator.Validate(ToPayload("Mock_Player1"));

    assert(result.result == dnf::LoginSuccess);
    assert(result.playerName == "Mock_Player1");
}

void TestEmptyName()
{
    const dnf::LoginValidator validator;
    const auto result = validator.Validate({});

    assert(result.result == dnf::EmptyPlayerName);
}

void TestLongName()
{
    const dnf::LoginValidator validator;
    const std::string longName(dnf::MAX_PLAYER_NAME_LENGTH + 1, 'A');
    const auto result = validator.Validate(ToPayload(longName));

    assert(result.result == dnf::PlayerNameTooLong);
}

void TestInvalidCharacter()
{
    const dnf::LoginValidator validator;
    const auto result = validator.Validate(ToPayload("Bad Name"));

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
