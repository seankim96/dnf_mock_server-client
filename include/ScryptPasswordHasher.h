#pragma once

#include "PasswordHasher.h"

namespace dnf
{
class ScryptPasswordHasher final : public PasswordHasher
{
public:
    std::optional<std::string> Hash(
        const std::string& password) override;
    bool Verify(
        const std::string& password,
        const std::string& encodedPasswordHash) override;
};
} // namespace dnf
