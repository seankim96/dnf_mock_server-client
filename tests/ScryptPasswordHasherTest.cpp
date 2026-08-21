#include "ScryptPasswordHasher.h"

#include "Account.h"

#include <cassert>
#include <iostream>
#include <string>

namespace
{
void TestHashCanVerifyCorrectPassword()
{
    dnf::ScryptPasswordHasher passwordHasher;
    const std::optional<std::string> encoded =
        passwordHasher.Hash("correct horse battery staple");

    assert(encoded.has_value());
    assert(encoded->starts_with("scrypt$131072$8$1$"));
    assert(encoded->size() <= dnf::MAX_ENCODED_PASSWORD_HASH_LENGTH);
    assert(passwordHasher.Verify(
        "correct horse battery staple",
        encoded.value()));
    assert(!passwordHasher.Verify("wrong password", encoded.value()));
}

void TestEachHashUsesADifferentSalt()
{
    dnf::ScryptPasswordHasher passwordHasher;
    const auto first = passwordHasher.Hash("same password");
    const auto second = passwordHasher.Hash("same password");

    assert(first.has_value());
    assert(second.has_value());
    assert(first.value() != second.value());
}

void TestInvalidInputOrHashIsRejected()
{
    dnf::ScryptPasswordHasher passwordHasher;
    assert(!passwordHasher.Hash("").has_value());

    const std::string longPassword(dnf::MAX_PASSWORD_LENGTH + 1, 'a');
    assert(!passwordHasher.Hash(longPassword).has_value());
    assert(!passwordHasher.Verify("password", "plain-text"));
    assert(!passwordHasher.Verify(
        "password",
        "scrypt$3$8$1$00000000000000000000000000000000$"
        "0000000000000000000000000000000000000000000000000000000000000000"));
}
} // namespace

int main()
{
    TestHashCanVerifyCorrectPassword();
    TestEachHashUsesADifferentSalt();
    TestInvalidInputOrHashIsRejected();

    std::cout << "All scrypt password hasher tests passed.\n";
    return 0;
}
